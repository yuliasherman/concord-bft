// Concord
//
// Copyright (c) 2020-2021 VMware, Inc. All Rights Reserved.
//
// This product is licensed to you under the Apache 2.0 license (the "License"). You may not use this product except in
// compliance with the Apache 2.0 License.
//
// This product may include a number of subcomponents with separate copyright notices and license terms. Your use of
// these subcomponents is subject to the terms and conditions of the sub-component's license, as noted in the LICENSE
// file.

#include "ClientsManager.hpp"
#include "gtest/gtest.h"
#include <chrono>
#include <thread>

using namespace std;
using namespace bftEngine;

TEST(ClientsManager, reservedPagesPerClient) {
  const uint32_t sizeOfReservedPage = 1024;
  const uint16_t maxBatchSize = 5;
  uint32_t maxReplySize = 1000;
  auto numPagesPerCl =
      bftEngine::impl::ClientsManager::reservedPagesPerClient(sizeOfReservedPage, maxReplySize, maxBatchSize);
  ASSERT_EQ(numPagesPerCl, 5);
  maxReplySize = 3000;
  numPagesPerCl =
      bftEngine::impl::ClientsManager::reservedPagesPerClient(sizeOfReservedPage, maxReplySize, maxBatchSize);
  ASSERT_EQ(numPagesPerCl, 15);
  maxReplySize = 1024;
  numPagesPerCl =
      bftEngine::impl::ClientsManager::reservedPagesPerClient(sizeOfReservedPage, maxReplySize, maxBatchSize);
  ASSERT_EQ(numPagesPerCl, 5);
}

TEST(ClientsManager, constructor) {
  std::set<bftEngine::impl::NodeIdType> clset{1, 4, 50, 7};
  bftEngine::impl::ClientsManager cm{clset};
  ASSERT_EQ(50, cm.getHighestClientId());
  auto i = 0;
  for (auto id : clset) {
    ASSERT_EQ(i, cm.getIndexOfClient(id));
    ASSERT_EQ(false, cm.isInternalCustomer(id));
    ++i;
  }
}

// Test that replicas are added to Client manager data structures and are identified correctly
TEST(ClientsManager, initClientInfo) {
  std::set<bftEngine::impl::NodeIdType> clset{1, 4, 50, 7};
  bftEngine::impl::ClientsManager cm{clset};
  auto firstReplicaId = cm.getHighestClientId() + 1;
  auto firstIntIdx = cm.getIndexOfClient(cm.getHighestClientId()) + 1;
  auto numRep = 7;
  cm.initInternalCustomerInfo(numRep);
  for (int i = 0; i < numRep; i++) {
    ASSERT_EQ(true, cm.isValidClient(firstReplicaId + i));
    ASSERT_EQ(true, cm.isInternalCustomer(firstReplicaId + 1));
    ASSERT_EQ(firstIntIdx + i, cm.getIndexOfClient(firstReplicaId + i));
  }
  // One over the end
  ASSERT_EQ(false, cm.isValidClient(firstReplicaId + numRep));
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}