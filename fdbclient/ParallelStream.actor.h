/*
 * ParallelStream.actor.h
 *
 * This source file is part of the FoundationDB open source project
 *
 * Copyright 2013-2018 Apple Inc. and the FoundationDB project authors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

// When actually compiled (NO_INTELLISENSE), include the generated version of this file.  In intellisense use the source
// version.
#if defined(NO_INTELLISENSE) && !defined(FDBCLIENT_PARALLEL_STREAM_ACTOR_G_H)
#define FDBCLIENT_PARALLEL_STREAM_ACTOR_G_H
#include "fdbclient/ParallelStream.actor.g.h"
#elif !defined(FDBCLIENT_PARALLEL_STREAM_ACTOR_H)
#define FDBCLIENT_PARALLEL_STREAM_ACTOR_H

#include <deque>
#include <vector>

#include "flow/genericactors.actor.h"
#include "flow/actorcompiler.h" // must be last include

template <class T>
class ParallelStream {
	FlowLock semaphore;

public:
	class Fragment {
		ParallelStream* parallelStream;
		std::deque<T> buffer;
		bool completed{ false };
		friend class ParallelStream;
		FlowLock::Releaser releaser;
	public:
		Fragment(ParallelStream* parallelStream)
		  : parallelStream(parallelStream), releaser(parallelStream->semaphore) {}
		void send(const T& value) {
			buffer.push_back(value);
			parallelStream->flushToClient();
		}
		void sendError(Error e) { parallelStream->sendError(e); }
		void finish() {
			ASSERT(!completed);
			completed = true;
			parallelStream->flushToClient();
			releaser.release();
		}
	};

private:
	std::vector<std::unique_ptr<Fragment>> fragments;
	typename std::vector<std::unique_ptr<Fragment>>::iterator nextFragment;
	PromiseStream<T> results;

	// TODO: Fix potential slow task
	void flushToClient() {
		while (nextFragment != fragments.end()) {
			if (!*nextFragment) {
				++nextFragment;
				continue;
			}
			auto fragment = nextFragment->get();
			while (!fragment->buffer.empty()) {
				results.send(fragment->buffer.front());
				fragment->buffer.pop_front();
			}
			if (fragment->completed) {
				nextFragment->reset();
				++nextFragment;
			} else {
				break;
			}
		}
	}

public:
	ParallelStream(PromiseStream<T> results, size_t concurrency) : results(results), semaphore(concurrency) {}

	ACTOR static Future<Fragment*> createFragmentImpl(ParallelStream<T>* self) {
		wait(self->semaphore.take());
		self->fragments.push_back(std::make_unique<Fragment>(self));
		if (self->fragments.size() == 1) {
			self->nextFragment = self->fragments.begin();
		}
		ASSERT(*self->nextFragment);
		return self->fragments.back().get();
	}

	Future<Fragment*> createFragment() { return createFragmentImpl(this); }

	void sendError(Error e) { results.sendError(e); }
};

#include "flow/unactorcompiler.h"

#endif
