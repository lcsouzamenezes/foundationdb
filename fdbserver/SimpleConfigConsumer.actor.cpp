/*
 * SimpleConfigConsumer.actor.cpp
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

#include "fdbserver/ConfigBroadcastFollowerInterface.h"
#include "fdbserver/SimpleConfigConsumer.h"

class SimpleConfigConsumerImpl {
	ConfigFollowerInterface cfi;
	Version lastSeenVersion{ 0 };
	ConfigClassSet configClassSet;
	Optional<double> pollingInterval;
	Optional<double> compactionInterval;

	UID id;
	CounterCollection cc;
	Counter compactRequest;
	Counter successfulChangeRequest;
	Counter failedChangeRequest;
	Counter snapshotRequest;
	Future<Void> logger;

	ACTOR static Future<Void> compactor(SimpleConfigConsumerImpl* self) {
		if (!self->compactionInterval.present()) {
			wait(Never());
			return Void();
		}
		loop {
			wait(delayJittered(self->compactionInterval.get()));
			// TODO: Enable compaction once bugs are fixed
			// wait(self->cfi.compact.getReply(ConfigFollowerCompactRequest{ self->lastSeenVersion }));
			//++self->compactRequest;
		}
	}

	ACTOR static Future<Void> fetchChanges(SimpleConfigConsumerImpl* self, ConfigBroadcaster* broadcaster) {
		wait(getSnapshot(self, broadcaster));
		loop {
			try {
				ConfigFollowerGetChangesReply reply =
				    wait(self->cfi.getChanges.getReply(ConfigFollowerGetChangesRequest{ self->lastSeenVersion }));
				++self->successfulChangeRequest;
				for (const auto& versionedMutation : reply.changes) {
					TraceEvent(SevDebug, "ConsumerFetchedMutation", self->id)
					    .detail("Version", versionedMutation.version)
					    .detail("ConfigClass", versionedMutation.mutation.getConfigClass())
					    .detail("KnobName", versionedMutation.mutation.getKnobName())
					    .detail("KnobValue", versionedMutation.mutation.getValue());
				}
				self->lastSeenVersion = reply.mostRecentVersion;
				broadcaster->applyChanges(reply.changes, reply.mostRecentVersion);
				if (self->pollingInterval.present()) {
					wait(delayJittered(self->pollingInterval.get()));
				}
			} catch (Error& e) {
				++self->failedChangeRequest;
				if (e.code() == error_code_version_already_compacted) {
					++self->snapshotRequest;
					wait(getSnapshot(self, broadcaster));
				} else {
					throw e;
				}
			}
		}
	}

	ACTOR static Future<Void> getSnapshot(SimpleConfigConsumerImpl* self, ConfigBroadcaster* broadcaster) {
		ConfigFollowerGetSnapshotAndChangesReply reply =
		    wait(self->cfi.getSnapshotAndChanges.getReply(ConfigFollowerGetSnapshotAndChangesRequest{}));
		TraceEvent(SevDebug, "BroadcasterGotSnapshot", self->id)
		    .detail("SnapshotVersion", reply.snapshotVersion)
		    .detail("SnapshotSize", reply.snapshot.size())
		    .detail("ChangesVersion", reply.changesVersion)
		    .detail("ChangesSize", reply.changes.size());
		broadcaster->applySnapshotAndChanges(
		    std::move(reply.snapshot), reply.snapshotVersion, reply.changes, reply.changesVersion);
		self->lastSeenVersion = reply.changesVersion;
		return Void();
	}

	static ConfigFollowerInterface getConfigFollowerInterface(ConfigFollowerInterface const& cfi) { return cfi; }

	static ConfigFollowerInterface getConfigFollowerInterface(ClusterConnectionString const& ccs) {
		auto coordinators = ccs.coordinators();
		std::sort(coordinators.begin(), coordinators.end());
		return ConfigFollowerInterface(coordinators[0]);
	}

	static ConfigFollowerInterface getConfigFollowerInterface(ServerCoordinators const& coordinators) {
		return ConfigFollowerInterface(coordinators.configServers[0]);
	}

public:
	template <class ConfigSource>
	SimpleConfigConsumerImpl(ConfigSource const& configSource,
	                         double const& pollingInterval,
	                         Optional<double> const& compactionInterval)
	  : pollingInterval(pollingInterval), compactionInterval(compactionInterval),
	    id(deterministicRandom()->randomUniqueID()), cc("ConfigConsumer"), compactRequest("CompactRequest", cc),
	    successfulChangeRequest("SuccessfulChangeRequest", cc), failedChangeRequest("FailedChangeRequest", cc),
	    snapshotRequest("SnapshotRequest", cc) {
		cfi = getConfigFollowerInterface(configSource);
		logger = traceCounters(
		    "ConfigConsumerMetrics", id, SERVER_KNOBS->WORKER_LOGGING_INTERVAL, &cc, "ConfigConsumerMetrics");
	}

	Future<Void> consume(ConfigBroadcaster& broadcaster) { return fetchChanges(this, &broadcaster) || compactor(this); }

	UID getID() const { return id; }
};

SimpleConfigConsumer::SimpleConfigConsumer(ConfigFollowerInterface const& cfi,
                                           double pollingInterval,
                                           Optional<double> compactionInterval)
  : impl(std::make_unique<SimpleConfigConsumerImpl>(cfi, pollingInterval, compactionInterval)) {}

SimpleConfigConsumer::SimpleConfigConsumer(ServerCoordinators const& coordinators,
                                           double pollingInterval,
                                           Optional<double> compactionInterval)
  : impl(std::make_unique<SimpleConfigConsumerImpl>(coordinators, pollingInterval, compactionInterval)) {}

Future<Void> SimpleConfigConsumer::consume(ConfigBroadcaster& broadcaster) {
	return impl->consume(broadcaster);
}

SimpleConfigConsumer::~SimpleConfigConsumer() = default;

UID SimpleConfigConsumer::getID() const {
	return impl->getID();
}
