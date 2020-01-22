// Concord
//
// Copyright (c) 2018-2019 VMware, Inc. All Rights Reserved.
//
// This product is licensed to you under the Apache 2.0 license (the "License").  You may not use this product except in
// compliance with the Apache 2.0 License.
//
// This product may include a number of subcomponents with separate copyright notices and license terms. Your use of
// these subcomponents is subject to the terms and conditions of the subcomponent's license, as noted in the LICENSE
// file.

#pragma once

#include <cstdint>

namespace bftEngine::impl {

class MsgCode {
 public:
  enum : uint16_t {
    None = 0,

    PrePrepare = 100,
    PreparePartial = 101,
    PrepareFull = 102,
    CommitPartial = 103,
    CommitFull = 104,
    StartSlowCommit = 105,
    PartialCommitProof = 106,
    FullCommitProof = 107,
    PartialExecProof = 108,
    FullExecProof = 109,
    SimpleAck = 110,
    ViewChange = 111,
    NewView = 112,
    Checkpoint = 113,
    ReplicaStatus = 114,
    ReqMissingData = 115,
    StateTransfer = 116,

    ClientPreProcessRequest = 117,
    PreProcessRequest = 118,
    PreProcessReply = 119,

    ClientRequest = 700,
    ClientReply = 800,

  };
};

}  // namespace bftEngine::impl
