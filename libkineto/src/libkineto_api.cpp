/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "libkineto.h"

#include "ConfigLoader.h"
#include "ThreadUtil.h"

namespace libkineto {

namespace plugin {
  static std::vector<ChildActivityProfilerFactory> plugin_factories;
  static std::mutex mutex;

  void SetKinetoPluginRegister(ChildActivityProfilerFactory factory) {
    std::scoped_lock<std::mutex> lock(mutex);
    plugin_factories.push_back(factory);
  }

  void ClearKinetoPluginFactory() {
    std::scoped_lock<std::mutex> lock(mutex);
    plugin_factories.clear();
  }
}

LibkinetoApi& api() {
  static LibkinetoApi instance(ConfigLoader::instance());
  return instance;
}

void LibkinetoApi::initClientIfRegistered() {
  if (client_) {
    if (clientRegisterThread_ != threadId()) {
      fprintf(
          stderr,
          "ERROR: External init callback must run in same thread as registerClient "
          "(%d != %d)\n",
          threadId(),
          (int)clientRegisterThread_);
    } else {
      client_->init();
    }
  }
}

void LibkinetoApi::registerClient(ClientInterface* client) {
  client_ = client;
  if (client && activityProfiler_) {
    // Can initialize straight away
    client->init();
  }
  // Assume here that the external init callback is *not* threadsafe
  // and only call it if it's the same thread that called registerClient
  clientRegisterThread_ = threadId();
}

void LibkinetoApi::initChildActivityProfilers() {
  if (!isProfilerInitialized()) {
    return;
  }
  for (const auto& factory : plugin::plugin_factories) {
    activityProfiler_->addChildActivityProfiler(factory());
  }
  plugin::ClearKinetoPluginFactory();
}

void LibkinetoApi::registerProfilerFactory(
  ChildActivityProfilerFactory factory) {
  if (isProfilerInitialized()) {
    activityProfiler_->addChildActivityProfiler(factory());
  } else {
    plugin::SetKinetoPluginRegister(factory);
  }
}

} // namespace libkineto
