// Concord
//
// Copyright (c) 2018-2019 VMware, Inc. All Rights Reserved.
//
// This product is licensed to you under the Apache 2.0 license (the "License").
// You may not use this product except in compliance with the Apache 2.0
// License.
//
// This product may include a number of subcomponents with separate copyright
// notices and license terms. Your use of these subcomponents is subject to the
// terms and conditions of the subcomponent's license, as noted in the LICENSE
// file.

#include "basicRandomTestsRunner.hpp"
#include "SimpleClient.hpp"
#include <chrono>
#include <fstream>
#include <histogram.hpp>

#ifndef _WIN32
#include <unistd.h>
#endif

using std::chrono::seconds;

using concord::kvbc::IClient;

namespace BasicRandomTests {

BasicRandomTestsRunner::BasicRandomTestsRunner(concordlogger::Logger &logger, IClient &client, size_t numOfOperations)
    : logger_(logger), client_(client), numOfOperations_(numOfOperations) {
  // We have to start the client here, since construction of the TestsBuilder
  // uses the client.
  assert(!client_.isRunning());
  client_.start();
  testsBuilder_ = new TestsBuilder(logger_, client);
}

void BasicRandomTestsRunner::sleep(int ops) {
#ifndef _WIN32
  if (ops % 100 == 0) usleep(100 * 1000);
#endif
}

void BasicRandomTestsRunner::run() {
  testsBuilder_->createRandomTest(numOfOperations_, 1111);

  concordUtils::Histogram hist;
  hist.Clear();

  RequestsList requests = testsBuilder_->getRequests();
  RepliesList expectedReplies = testsBuilder_->getReplies();
  assert(requests.size() == expectedReplies.size());

  int ops = 0;
  const auto start = std::chrono::steady_clock::now();
  while (!requests.empty()) {
    // sleep(ops);
    SimpleRequest *request = requests.front();
    SimpleReply *expectedReply = expectedReplies.front();
    requests.pop_front();
    expectedReplies.pop_front();

    size_t requestSize = TestsBuilder::sizeOfRequest(request);
    size_t expectedReplySize = TestsBuilder::sizeOfReply(expectedReply);
    uint32_t actualReplySize = 0;

    std::vector<char> reply(expectedReplySize);

    uint8_t flags = 0;
    if (request->type == PRE_PROCESS)
      flags = bftEngine::PRE_PROCESS_REQ;
    else if (request->type != COND_WRITE)
      flags = bftEngine::READ_ONLY_REQ;

    const auto startReq = std::chrono::steady_clock::now();
    auto res = client_.invokeCommandSynch(
        (char *)request, requestSize, flags, seconds(0), expectedReplySize, reply.data(), &actualReplySize);
    assert(res.isOK());

    if (!((SimpleCondWriteRequest *)request)->isLong)
      hist.Add(
          std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - startReq).count());

    // TBD: revive it
    // if (isReplyCorrect(request->type, expectedReply, reply.data(), expectedReplySize, actualReplySize)) ops++;
    ops++;
  }
  auto testTime =
      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
  sleep(1);
  LOG_INFO(logger_, "\n*** Test completed. " << ops << " messages have been handled.");

  std::fstream out("/tmp/concordbft/results/clients/" + std::to_string(client_.getClientID()) + "/summary.csv",
                   std::ios::out);
  if (out.is_open()) {
    auto res = hist.stringAllBuckets();
    //        out << std::get<0>(res) << std::endl;
    out << std::get<1>(res) << std::endl;
    out.close();
    std::fstream out1("/tmp/concordbft/results/clients/" + std::to_string(client_.getClientID()) + "/summary1.csv",
                      std::ios::out);
    out1 << std::to_string(client_.getClientID()) << "," << hist.size() << "," << hist.min() << ","
         << hist.Percentile(0.25) << "," << hist.Median() << "," << hist.Percentile(0.75) << "," << hist.max() << ","
         << hist.Average() << "," << hist.StandardDeviation() << "," << testTime << std::endl;
    out1.close();
  } else {
    LOG_ERROR(logger_, "unable to open output file");
  }

  client_.stop();
}

bool BasicRandomTestsRunner::isReplyCorrect(RequestType requestType,
                                            const SimpleReply *expectedReply,
                                            const char *reply,
                                            size_t expectedReplySize,
                                            uint32_t actualReplySize) {
  if (actualReplySize != expectedReplySize) {
    LOG_ERROR(logger_, "*** Test failed: actual reply size != expected");
    assert(0);
  }
  std::ostringstream error;
  switch (requestType) {
    case COND_WRITE:
      if (((SimpleReply_ConditionalWrite *)expectedReply)->isEquiv(*(SimpleReply_ConditionalWrite *)reply, error))
        return true;
      break;
    case READ:
      if (((SimpleReply_Read *)expectedReply)->isEquiv(*(SimpleReply_Read *)reply, error)) return true;
      break;
    case GET_LAST_BLOCK:
      if (((SimpleReply_GetLastBlock *)expectedReply)->isEquiv(*(SimpleReply_GetLastBlock *)reply, error)) return true;
      break;
    default:;
  }

  LOG_ERROR(logger_, "*** Test failed: actual reply != expected; error: " << error.str());
  assert(0);
  return false;
}

}  // namespace BasicRandomTests
