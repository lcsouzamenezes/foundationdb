/*
 * ConfigBroadcaster.h
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

#include "fdbclient/CoordinationInterface.h"
#include "fdbclient/JsonBuilder.h"
#include "fdbserver/CoordinationInterface.h"
#include "fdbserver/ConfigBroadcastFollowerInterface.h"
#include "fdbserver/ConfigFollowerInterface.h"
#include "flow/flow.h"
#include <memory>

class ConfigBroadcaster {
	std::unique_ptr<class ConfigBroadcasterImpl> impl;

public:
	explicit ConfigBroadcaster(ServerCoordinators const&, Optional<bool> useTestConfigDB);
	ConfigBroadcaster(ConfigBroadcaster&&);
	ConfigBroadcaster& operator=(ConfigBroadcaster&&);
	~ConfigBroadcaster();
	Future<Void> serve(ConfigBroadcastFollowerInterface const&);
	void applyChanges(Standalone<VectorRef<VersionedConfigMutationRef>> const& changes, Version mostRecentVersion);
	void applySnapshotAndChanges(std::map<ConfigKey, Value> const& snapshot,
	                             Version snapshotVersion,
	                             Standalone<VectorRef<VersionedConfigMutationRef>> const& changes,
	                             Version changesVersion);
	void applySnapshotAndChanges(std::map<ConfigKey, Value>&& snapshot,
	                             Version snapshotVersion,
	                             Standalone<VectorRef<VersionedConfigMutationRef>> const& changes,
	                             Version changesVersion);
	UID getID() const;
	JsonBuilderObject getStatus() const;
	void compact();

public: // Testing
	explicit ConfigBroadcaster(ConfigFollowerInterface const&);
};
