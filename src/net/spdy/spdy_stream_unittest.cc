// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/spdy/spdy_stream.h"

#include <stdint.h>

#include <algorithm>
#include <cstddef>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/bind.h"
#include "base/memory/ref_counted.h"
#include "base/run_loop.h"
#include "base/stl_util.h"
#include "base/strings/string_piece.h"
#include "net/base/request_priority.h"
#include "net/http/http_request_info.h"
#include "net/log/net_log_event_type.h"
#include "net/log/test_net_log.h"
#include "net/log/test_net_log_util.h"
#include "net/socket/socket_tag.h"
#include "net/socket/socket_test_util.h"
#include "net/spdy/buffered_spdy_framer.h"
#include "net/spdy/http2_push_promise_index.h"
#include "net/spdy/spdy_http_utils.h"
#include "net/spdy/spdy_session.h"
#include "net/spdy/spdy_session_pool.h"
#include "net/spdy/spdy_stream_test_util.h"
#include "net/spdy/spdy_test_util_common.h"
#include "net/test/cert_test_util.h"
#include "net/test/gtest_util.h"
#include "net/test/test_data_directory.h"
#include "net/test/test_with_task_environment.h"
#include "net/third_party/quiche/src/spdy/core/spdy_protocol.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

// TODO(ukai): factor out common part with spdy_http_stream_unittest.cc
//
namespace net {

namespace test {

namespace {

const char kPushUrl[] = "https://www.example.org/push";
const char kPostBody[] = "\0hello!\xff";
const size_t kPostBodyLength = base::size(kPostBody);
const base::StringPiece kPostBodyStringPiece(kPostBody, kPostBodyLength);

static base::TimeTicks g_time_now;

base::TimeTicks InstantaneousReads() {
  return g_time_now;
}

// Creates a MockRead from the given serialized frame except for the last byte.
MockRead ReadFrameExceptForLastByte(const spdy::SpdySerializedFrame& frame) {
  CHECK_GE(frame.size(), 2u);
  return MockRead(ASYNC, frame.data(), frame.size() - 1);
}

// Creates a MockRead from the last byte of the given serialized frame.
MockRead LastByteOfReadFrame(const spdy::SpdySerializedFrame& frame) {
  CHECK_GE(frame.size(), 2u);
  return MockRead(ASYNC, frame.data() + frame.size() - 1, 1);
}

}  // namespace

class SpdyStreamTest : public ::testing::Test, public WithTaskEnvironment {
 protected:
  // A function that takes a SpdyStream and the number of bytes which
  // will unstall the next frame completely.
  typedef base::OnceCallback<void(const base::WeakPtr<SpdyStream>&, int32_t)>
      UnstallFunction;

  explicit SpdyStreamTest(base::test::TaskEnvironment::TimeSource time_source =
                              base::test::TaskEnvironment::TimeSource::DEFAULT)
      : WithTaskEnvironment(time_source),
        url_(kDefaultUrl),
        session_(SpdySessionDependencies::SpdyCreateSession(&session_deps_)),
        offset_(0),
        ssl_(SYNCHRONOUS, OK) {}

  ~SpdyStreamTest() override = default;

  base::WeakPtr<SpdySession> CreateDefaultSpdySession() {
    SpdySessionKey key(HostPortPair::FromURL(url_), ProxyServer::Direct(),
                       PRIVACY_MODE_DISABLED,
                       SpdySessionKey::IsProxySession::kFalse, SocketTag(),
                       NetworkIsolationKey(), false /* disable_secure_dns */);
    return CreateSpdySession(session_.get(), key, NetLogWithSource());
  }

  void TearDown() override { base::RunLoop().RunUntilIdle(); }

  void RunResumeAfterUnstallRequestResponseTest(
      UnstallFunction unstall_function);

  void RunResumeAfterUnstallBidirectionalTest(UnstallFunction unstall_function);

  // Add{Read,Write}() populates lists that are eventually passed to a
  // SocketData class. |frame| must live for the whole test.

  void AddRead(const spdy::SpdySerializedFrame& frame) {
    reads_.push_back(CreateMockRead(frame, offset_++));
  }

  void AddWrite(const spdy::SpdySerializedFrame& frame) {
    writes_.push_back(CreateMockWrite(frame, offset_++));
  }

  void AddMockRead(MockRead read) {
    read.sequence_number = offset_++;
    reads_.push_back(std::move(read));
  }

  void AddReadEOF() {
    reads_.push_back(MockRead(ASYNC, 0, offset_++));
  }

  void AddWritePause() {
    writes_.push_back(MockWrite(ASYNC, ERR_IO_PENDING, offset_++));
  }

  void AddReadPause() {
    reads_.push_back(MockRead(ASYNC, ERR_IO_PENDING, offset_++));
  }

  base::span<const MockRead> GetReads() { return reads_; }
  base::span<const MockWrite> GetWrites() { return writes_; }

  void ActivatePushStream(SpdySession* session, SpdyStream* stream) {
    std::unique_ptr<SpdyStream> activated =
        session->ActivateCreatedStream(stream);
    activated->set_stream_id(2);
    session->InsertActivatedStream(std::move(activated));
  }

  void AddSSLSocketData() {
    // Load a cert that is valid for
    // www.example.org, mail.example.org, and mail.example.com.
    ssl_.ssl_info.cert =
        ImportCertFromFile(GetTestCertsDirectory(), "spdy_pooling.pem");
    ASSERT_TRUE(ssl_.ssl_info.cert);
    session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl_);
  }

  static size_t num_pushed_streams(base::WeakPtr<SpdySession> session) {
    return session->num_pushed_streams_;
  }

  static SpdySessionPool* spdy_session_pool(
      base::WeakPtr<SpdySession> session) {
    return session->pool_;
  }

  const GURL url_;
  SpdyTestUtil spdy_util_;
  SpdySessionDependencies session_deps_;
  std::unique_ptr<HttpNetworkSession> session_;

 private:
  // Used by Add{Read,Write}() above.
  std::vector<MockWrite> writes_;
  std::vector<MockRead> reads_;
  int offset_;
  SSLSocketDataProvider ssl_;
};

TEST_F(SpdyStreamTest, SendDataAfterOpen) {
  spdy::SpdySerializedFrame req(spdy_util_.ConstructSpdyPost(
      kDefaultUrl, 1, kPostBodyLength, LOWEST, nullptr, 0));
  AddWrite(req);

  spdy::SpdySerializedFrame resp(spdy_util_.ConstructSpdyPostReply(nullptr, 0));
  AddRead(resp);

  spdy::SpdySerializedFrame msg(
      spdy_util_.ConstructSpdyDataFrame(1, kPostBodyStringPiece, false));
  AddWrite(msg);

  spdy::SpdySerializedFrame echo(
      spdy_util_.ConstructSpdyDataFrame(1, kPostBodyStringPiece, false));
  AddRead(echo);

  AddReadEOF();

  SequencedSocketData data(GetReads(), GetWrites());
  MockConnect connect_data(SYNCHRONOUS, OK);
  data.set_connect_data(connect_data);
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  AddSSLSocketData();

  base::WeakPtr<SpdySession> session(CreateDefaultSpdySession());

  base::WeakPtr<SpdyStream> stream = CreateStreamSynchronously(
      SPDY_BIDIRECTIONAL_STREAM, session, url_, LOWEST, NetLogWithSource());
  ASSERT_TRUE(stream);
  EXPECT_EQ(kDefaultUrl, stream->url().spec());

  StreamDelegateSendImmediate delegate(stream, kPostBodyStringPiece);
  stream->SetDelegate(&delegate);

  spdy::Http2HeaderBlock headers(
      spdy_util_.ConstructPostHeaderBlock(kDefaultUrl, kPostBodyLength));
  EXPECT_THAT(stream->SendRequestHeaders(std::move(headers), MORE_DATA_TO_SEND),
              IsError(ERR_IO_PENDING));

  EXPECT_THAT(delegate.WaitForClose(), IsError(ERR_CONNECTION_CLOSED));

  EXPECT_TRUE(delegate.send_headers_completed());
  EXPECT_EQ("200", delegate.GetResponseHeaderValue(spdy::kHttp2StatusHeader));
  EXPECT_EQ(std::string(kPostBody, kPostBodyLength),
            delegate.TakeReceivedData());
  EXPECT_TRUE(data.AllWriteDataConsumed());
}

// Delegate that receives trailers.
class StreamDelegateWithTrailers : public test::StreamDelegateWithBody {
 public:
  StreamDelegateWithTrailers(const base::WeakPtr<SpdyStream>& stream,
                             base::StringPiece data)
      : StreamDelegateWithBody(stream, data) {}

  ~StreamDelegateWithTrailers() override = default;

  void OnTrailers(const spdy::Http2HeaderBlock& trailers) override {
    trailers_ = trailers.Clone();
  }

  const spdy::Http2HeaderBlock& trailers() const { return trailers_; }

 private:
  spdy::Http2HeaderBlock trailers_;
};

// Regression test for https://crbug.com/481033.
TEST_F(SpdyStreamTest, Trailers) {
  spdy::SpdySerializedFrame req(spdy_util_.ConstructSpdyPost(
      kDefaultUrl, 1, kPostBodyLength, LOWEST, nullptr, 0));
  AddWrite(req);

  spdy::SpdySerializedFrame msg(
      spdy_util_.ConstructSpdyDataFrame(1, kPostBodyStringPiece, true));
  AddWrite(msg);

  spdy::SpdySerializedFrame resp(spdy_util_.ConstructSpdyPostReply(nullptr, 0));
  AddRead(resp);

  spdy::SpdySerializedFrame echo(
      spdy_util_.ConstructSpdyDataFrame(1, kPostBodyStringPiece, false));
  AddRead(echo);

  spdy::Http2HeaderBlock late_headers;
  late_headers["foo"] = "bar";
  spdy::SpdySerializedFrame trailers(spdy_util_.ConstructSpdyResponseHeaders(
      1, std::move(late_headers), false));
  AddRead(trailers);

  AddReadEOF();

  SequencedSocketData data(GetReads(), GetWrites());
  MockConnect connect_data(SYNCHRONOUS, OK);
  data.set_connect_data(connect_data);
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  AddSSLSocketData();

  base::WeakPtr<SpdySession> session(CreateDefaultSpdySession());

  base::WeakPtr<SpdyStream> stream = CreateStreamSynchronously(
      SPDY_REQUEST_RESPONSE_STREAM, session, url_, LOWEST, NetLogWithSource());
  ASSERT_TRUE(stream);
  EXPECT_EQ(kDefaultUrl, stream->url().spec());

  StreamDelegateWithTrailers delegate(stream, kPostBodyStringPiece);
  stream->SetDelegate(&delegate);

  spdy::Http2HeaderBlock headers(
      spdy_util_.ConstructPostHeaderBlock(kDefaultUrl, kPostBodyLength));
  EXPECT_THAT(stream->SendRequestHeaders(std::move(headers), MORE_DATA_TO_SEND),
              IsError(ERR_IO_PENDING));

  EXPECT_THAT(delegate.WaitForClose(), IsError(ERR_CONNECTION_CLOSED));

  EXPECT_TRUE(delegate.send_headers_completed());
  EXPECT_EQ("200", delegate.GetResponseHeaderValue(spdy::kHttp2StatusHeader));
  const spdy::Http2HeaderBlock& received_trailers = delegate.trailers();
  spdy::Http2HeaderBlock::const_iterator it = received_trailers.find("foo");
  EXPECT_EQ("bar", it->second);
  EXPECT_EQ(std::string(kPostBody, kPostBodyLength),
            delegate.TakeReceivedData());
  EXPECT_TRUE(data.AllWriteDataConsumed());
}

TEST_F(SpdyStreamTest, PushedStream) {
  spdy::SpdySerializedFrame req(
      spdy_util_.ConstructSpdyGet(nullptr, 0, 1, LOWEST));
  AddWrite(req);

  spdy::SpdySerializedFrame reply(
      spdy_util_.ConstructSpdyGetReply(nullptr, 0, 1));
  AddRead(reply);

  spdy::SpdySerializedFrame push(
      spdy_util_.ConstructSpdyPush(nullptr, 0, 2, 1, kPushUrl));
  AddRead(push);

  spdy::SpdySerializedFrame priority(
      spdy_util_.ConstructSpdyPriority(2, 1, IDLE, true));
  AddWrite(priority);

  AddReadPause();

  base::StringPiece pushed_msg("foo");
  spdy::SpdySerializedFrame pushed_body(
      spdy_util_.ConstructSpdyDataFrame(2, pushed_msg, true));
  AddRead(pushed_body);

  base::StringPiece msg("bar");
  spdy::SpdySerializedFrame body(
      spdy_util_.ConstructSpdyDataFrame(1, msg, true));
  AddRead(body);

  AddReadEOF();

  SequencedSocketData data(GetReads(), GetWrites());
  MockConnect connect_data(SYNCHRONOUS, OK);
  data.set_connect_data(connect_data);
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  AddSSLSocketData();

  g_time_now = base::TimeTicks::Now();
  session_deps_.time_func = InstantaneousReads;
  session_ = SpdySessionDependencies::SpdyCreateSession(&session_deps_);

  base::WeakPtr<SpdySession> session(CreateDefaultSpdySession());

  base::WeakPtr<SpdyStream> stream = CreateStreamSynchronously(
      SPDY_REQUEST_RESPONSE_STREAM, session, url_, LOWEST, NetLogWithSource());
  ASSERT_TRUE(stream);
  EXPECT_EQ(kDefaultUrl, stream->url().spec());

  StreamDelegateDoNothing delegate(stream);
  stream->SetDelegate(&delegate);

  spdy::Http2HeaderBlock headers(
      spdy_util_.ConstructGetHeaderBlock(kDefaultUrl));
  EXPECT_THAT(
      stream->SendRequestHeaders(std::move(headers), NO_MORE_DATA_TO_SEND),
      IsError(ERR_IO_PENDING));

  data.RunUntilPaused();

  const SpdySessionKey key(
      HostPortPair::FromURL(url_), ProxyServer::Direct(), PRIVACY_MODE_DISABLED,
      SpdySessionKey::IsProxySession::kFalse, SocketTag(),
      NetworkIsolationKey(), false /* disable_secure_dns */);
  const GURL pushed_url(kPushUrl);
  HttpRequestInfo push_request;
  push_request.url = pushed_url;
  push_request.method = "GET";
  base::WeakPtr<SpdySession> session_with_pushed_stream;
  spdy::SpdyStreamId pushed_stream_id;
  spdy_session_pool(session)->push_promise_index()->ClaimPushedStream(
      key, pushed_url, push_request, &session_with_pushed_stream,
      &pushed_stream_id);
  EXPECT_EQ(session.get(), session_with_pushed_stream.get());
  EXPECT_EQ(2u, pushed_stream_id);

  SpdyStream* push_stream;
  EXPECT_THAT(session->GetPushedStream(pushed_url, pushed_stream_id, IDLE,
                                       &push_stream),
              IsOk());
  ASSERT_TRUE(push_stream);
  EXPECT_EQ(kPushUrl, push_stream->url().spec());

  LoadTimingInfo load_timing_info;
  EXPECT_TRUE(push_stream->GetLoadTimingInfo(&load_timing_info));
  EXPECT_EQ(g_time_now, load_timing_info.push_start);
  EXPECT_TRUE(load_timing_info.push_end.is_null());

  StreamDelegateDoNothing push_delegate(push_stream->GetWeakPtr());
  push_stream->SetDelegate(&push_delegate);

  data.Resume();

  EXPECT_TRUE(push_stream->GetLoadTimingInfo(&load_timing_info));
  EXPECT_EQ(g_time_now, load_timing_info.push_start);
  EXPECT_FALSE(load_timing_info.push_end.is_null());

  EXPECT_THAT(delegate.WaitForClose(), IsOk());
  EXPECT_EQ("200", delegate.GetResponseHeaderValue(spdy::kHttp2StatusHeader));
  EXPECT_EQ(msg, delegate.TakeReceivedData());

  EXPECT_THAT(push_delegate.WaitForClose(), IsOk());
  EXPECT_EQ("200", delegate.GetResponseHeaderValue(spdy::kHttp2StatusHeader));
  EXPECT_EQ(pushed_msg, push_delegate.TakeReceivedData());

  // Finish async network reads and writes.
  base::RunLoop().RunUntilIdle();

  EXPECT_TRUE(data.AllWriteDataConsumed());
  EXPECT_TRUE(data.AllReadDataConsumed());
}

TEST_F(SpdyStreamTest, StreamError) {
  spdy::SpdySerializedFrame req(spdy_util_.ConstructSpdyPost(
      kDefaultUrl, 1, kPostBodyLength, LOWEST, nullptr, 0));
  AddWrite(req);

  spdy::SpdySerializedFrame resp(
      spdy_util_.ConstructSpdyGetReply(nullptr, 0, 1));
  AddRead(resp);

  spdy::SpdySerializedFrame msg(
      spdy_util_.ConstructSpdyDataFrame(1, kPostBodyStringPiece, false));
  AddWrite(msg);

  spdy::SpdySerializedFrame echo(
      spdy_util_.ConstructSpdyDataFrame(1, kPostBodyStringPiece, false));
  AddRead(echo);

  AddReadEOF();

  RecordingBoundTestNetLog log;

  SequencedSocketData data(GetReads(), GetWrites());
  MockConnect connect_data(SYNCHRONOUS, OK);
  data.set_connect_data(connect_data);
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  AddSSLSocketData();

  base::WeakPtr<SpdySession> session(CreateDefaultSpdySession());

  base::WeakPtr<SpdyStream> stream = CreateStreamSynchronously(
      SPDY_BIDIRECTIONAL_STREAM, session, url_, LOWEST, log.bound());
  ASSERT_TRUE(stream);
  EXPECT_EQ(kDefaultUrl, stream->url().spec());

  StreamDelegateSendImmediate delegate(stream, kPostBodyStringPiece);
  stream->SetDelegate(&delegate);

  spdy::Http2HeaderBlock headers(
      spdy_util_.ConstructPostHeaderBlock(kDefaultUrl, kPostBodyLength));
  EXPECT_THAT(stream->SendRequestHeaders(std::move(headers), MORE_DATA_TO_SEND),
              IsError(ERR_IO_PENDING));

  EXPECT_THAT(delegate.WaitForClose(), IsError(ERR_CONNECTION_CLOSED));

  const spdy::SpdyStreamId stream_id = delegate.stream_id();

  EXPECT_TRUE(delegate.send_headers_completed());
  EXPECT_EQ("200", delegate.GetResponseHeaderValue(spdy::kHttp2StatusHeader));
  EXPECT_EQ(std::string(kPostBody, kPostBodyLength),
            delegate.TakeReceivedData());
  EXPECT_TRUE(data.AllWriteDataConsumed());

  // Check that the NetLog was filled reasonably.
  auto entries = log.GetEntries();
  EXPECT_LT(0u, entries.size());

  // Check that we logged SPDY_STREAM_ERROR correctly.
  int pos = ExpectLogContainsSomewhere(
      entries, 0, NetLogEventType::HTTP2_STREAM_ERROR, NetLogEventPhase::NONE);

  EXPECT_EQ(static_cast<int>(stream_id),
            GetIntegerValueFromParams(entries[pos], "stream_id"));
}

// Make sure that large blocks of data are properly split up into frame-sized
// chunks for a request/response (i.e., an HTTP-like) stream.
TEST_F(SpdyStreamTest, SendLargeDataAfterOpenRequestResponse) {
  spdy::SpdySerializedFrame req(spdy_util_.ConstructSpdyPost(
      kDefaultUrl, 1, kPostBodyLength, LOWEST, nullptr, 0));
  AddWrite(req);

  std::string chunk_data(kMaxSpdyFrameChunkSize, 'x');
  spdy::SpdySerializedFrame chunk(
      spdy_util_.ConstructSpdyDataFrame(1, chunk_data, false));
  AddWrite(chunk);
  AddWrite(chunk);

  spdy::SpdySerializedFrame last_chunk(
      spdy_util_.ConstructSpdyDataFrame(1, chunk_data, true));
  AddWrite(last_chunk);

  spdy::SpdySerializedFrame resp(spdy_util_.ConstructSpdyPostReply(nullptr, 0));
  AddRead(resp);

  AddReadEOF();

  SequencedSocketData data(GetReads(), GetWrites());
  MockConnect connect_data(SYNCHRONOUS, OK);
  data.set_connect_data(connect_data);
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  AddSSLSocketData();

  base::WeakPtr<SpdySession> session(CreateDefaultSpdySession());

  base::WeakPtr<SpdyStream> stream = CreateStreamSynchronously(
      SPDY_REQUEST_RESPONSE_STREAM, session, url_, LOWEST, NetLogWithSource());
  ASSERT_TRUE(stream);
  EXPECT_EQ(kDefaultUrl, stream->url().spec());

  std::string body_data(3 * kMaxSpdyFrameChunkSize, 'x');
  StreamDelegateWithBody delegate(stream, body_data);
  stream->SetDelegate(&delegate);

  spdy::Http2HeaderBlock headers(
      spdy_util_.ConstructPostHeaderBlock(kDefaultUrl, kPostBodyLength));
  EXPECT_THAT(stream->SendRequestHeaders(std::move(headers), MORE_DATA_TO_SEND),
              IsError(ERR_IO_PENDING));

  EXPECT_THAT(delegate.WaitForClose(), IsError(ERR_CONNECTION_CLOSED));

  EXPECT_TRUE(delegate.send_headers_completed());
  EXPECT_EQ("200", delegate.GetResponseHeaderValue(spdy::kHttp2StatusHeader));
  EXPECT_EQ(std::string(), delegate.TakeReceivedData());
  EXPECT_TRUE(data.AllWriteDataConsumed());
}

// Make sure that large blocks of data are properly split up into frame-sized
// chunks for a bidirectional (i.e., non-HTTP-like) stream.
TEST_F(SpdyStreamTest, SendLargeDataAfterOpenBidirectional) {
  spdy::SpdySerializedFrame req(spdy_util_.ConstructSpdyPost(
      kDefaultUrl, 1, kPostBodyLength, LOWEST, nullptr, 0));
  AddWrite(req);

  spdy::SpdySerializedFrame resp(spdy_util_.ConstructSpdyPostReply(nullptr, 0));
  AddRead(resp);

  std::string chunk_data(kMaxSpdyFrameChunkSize, 'x');
  spdy::SpdySerializedFrame chunk(
      spdy_util_.ConstructSpdyDataFrame(1, chunk_data, false));
  AddWrite(chunk);
  AddWrite(chunk);
  AddWrite(chunk);

  AddReadEOF();

  SequencedSocketData data(GetReads(), GetWrites());
  MockConnect connect_data(SYNCHRONOUS, OK);
  data.set_connect_data(connect_data);
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  AddSSLSocketData();

  base::WeakPtr<SpdySession> session(CreateDefaultSpdySession());

  base::WeakPtr<SpdyStream> stream = CreateStreamSynchronously(
      SPDY_BIDIRECTIONAL_STREAM, session, url_, LOWEST, NetLogWithSource());
  ASSERT_TRUE(stream);
  EXPECT_EQ(kDefaultUrl, stream->url().spec());

  std::string body_data(3 * kMaxSpdyFrameChunkSize, 'x');
  StreamDelegateSendImmediate delegate(stream, body_data);
  stream->SetDelegate(&delegate);

  spdy::Http2HeaderBlock headers(
      spdy_util_.ConstructPostHeaderBlock(kDefaultUrl, kPostBodyLength));
  EXPECT_THAT(stream->SendRequestHeaders(std::move(headers), MORE_DATA_TO_SEND),
              IsError(ERR_IO_PENDING));

  EXPECT_THAT(delegate.WaitForClose(), IsError(ERR_CONNECTION_CLOSED));

  EXPECT_TRUE(delegate.send_headers_completed());
  EXPECT_EQ("200", delegate.GetResponseHeaderValue(spdy::kHttp2StatusHeader));
  EXPECT_EQ(std::string(), delegate.TakeReceivedData());
  EXPECT_TRUE(data.AllWriteDataConsumed());
}

// Receiving a header with uppercase ASCII should result in a protocol error.
TEST_F(SpdyStreamTest, UpperCaseHeaders) {
  spdy::SpdySerializedFrame req(
      spdy_util_.ConstructSpdyGet(nullptr, 0, 1, LOWEST));
  AddWrite(req);

  const char* const kExtraHeaders[] = {"X-UpperCase", "yes"};
  spdy::SpdySerializedFrame reply(spdy_util_.ConstructSpdyGetReply(
      kExtraHeaders, base::size(kExtraHeaders) / 2, 1));
  AddRead(reply);

  spdy::SpdySerializedFrame rst(
      spdy_util_.ConstructSpdyRstStream(1, spdy::ERROR_CODE_PROTOCOL_ERROR));
  AddWrite(rst);

  AddReadEOF();

  SequencedSocketData data(GetReads(), GetWrites());
  MockConnect connect_data(SYNCHRONOUS, OK);
  data.set_connect_data(connect_data);
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  AddSSLSocketData();

  base::WeakPtr<SpdySession> session(CreateDefaultSpdySession());

  base::WeakPtr<SpdyStream> stream = CreateStreamSynchronously(
      SPDY_REQUEST_RESPONSE_STREAM, session, url_, LOWEST, NetLogWithSource());
  ASSERT_TRUE(stream);
  EXPECT_EQ(kDefaultUrl, stream->url().spec());

  StreamDelegateDoNothing delegate(stream);
  stream->SetDelegate(&delegate);

  spdy::Http2HeaderBlock headers(
      spdy_util_.ConstructGetHeaderBlock(kDefaultUrl));
  EXPECT_THAT(
      stream->SendRequestHeaders(std::move(headers), NO_MORE_DATA_TO_SEND),
      IsError(ERR_IO_PENDING));

  EXPECT_THAT(delegate.WaitForClose(), IsError(ERR_HTTP2_PROTOCOL_ERROR));

  // Finish async network reads and writes.
  base::RunLoop().RunUntilIdle();

  EXPECT_TRUE(data.AllWriteDataConsumed());
  EXPECT_TRUE(data.AllReadDataConsumed());
}

// Receiving a header with uppercase ASCII should result in a protocol error
// even for a push stream.
TEST_F(SpdyStreamTest, UpperCaseHeadersOnPush) {
  spdy::SpdySerializedFrame req(
      spdy_util_.ConstructSpdyGet(nullptr, 0, 1, LOWEST));
  AddWrite(req);

  spdy::SpdySerializedFrame reply(
      spdy_util_.ConstructSpdyGetReply(nullptr, 0, 1));
  AddRead(reply);

  const char* const kExtraHeaders[] = {"X-UpperCase", "yes"};
  spdy::SpdySerializedFrame push(spdy_util_.ConstructSpdyPush(
      kExtraHeaders, base::size(kExtraHeaders) / 2, 2, 1, kPushUrl));
  AddRead(push);

  spdy::SpdySerializedFrame priority(
      spdy_util_.ConstructSpdyPriority(2, 1, IDLE, true));
  AddWrite(priority);

  spdy::SpdySerializedFrame rst(
      spdy_util_.ConstructSpdyRstStream(2, spdy::ERROR_CODE_PROTOCOL_ERROR));
  AddWrite(rst);

  AddReadPause();

  AddReadEOF();

  SequencedSocketData data(GetReads(), GetWrites());
  MockConnect connect_data(SYNCHRONOUS, OK);
  data.set_connect_data(connect_data);
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  AddSSLSocketData();

  base::WeakPtr<SpdySession> session(CreateDefaultSpdySession());

  base::WeakPtr<SpdyStream> stream = CreateStreamSynchronously(
      SPDY_REQUEST_RESPONSE_STREAM, session, url_, LOWEST, NetLogWithSource());
  ASSERT_TRUE(stream);
  EXPECT_EQ(kDefaultUrl, stream->url().spec());

  StreamDelegateDoNothing delegate(stream);
  stream->SetDelegate(&delegate);

  spdy::Http2HeaderBlock headers(
      spdy_util_.ConstructGetHeaderBlock(kDefaultUrl));
  EXPECT_THAT(
      stream->SendRequestHeaders(std::move(headers), NO_MORE_DATA_TO_SEND),
      IsError(ERR_IO_PENDING));

  data.RunUntilPaused();

  EXPECT_EQ(0u, num_pushed_streams(session));

  data.Resume();

  EXPECT_THAT(delegate.WaitForClose(), IsError(ERR_CONNECTION_CLOSED));

  EXPECT_TRUE(data.AllWriteDataConsumed());
  EXPECT_TRUE(data.AllReadDataConsumed());
}

TEST_F(SpdyStreamTest, HeadersMustHaveStatus) {
  spdy::SpdySerializedFrame req(
      spdy_util_.ConstructSpdyGet(nullptr, 0, 1, LOWEST));
  AddWrite(req);

  // Response headers without ":status" header field: protocol error.
  spdy::Http2HeaderBlock header_block_without_status;
  header_block_without_status[spdy::kHttp2MethodHeader] = "GET";
  header_block_without_status[spdy::kHttp2AuthorityHeader] = "www.example.org";
  header_block_without_status[spdy::kHttp2SchemeHeader] = "https";
  header_block_without_status[spdy::kHttp2PathHeader] = "/";
  spdy::SpdySerializedFrame reply(
      spdy_util_.ConstructSpdyReply(1, std::move(header_block_without_status)));
  AddRead(reply);

  spdy::SpdySerializedFrame rst(
      spdy_util_.ConstructSpdyRstStream(1, spdy::ERROR_CODE_PROTOCOL_ERROR));
  AddWrite(rst);

  AddReadEOF();

  SequencedSocketData data(GetReads(), GetWrites());
  MockConnect connect_data(SYNCHRONOUS, OK);
  data.set_connect_data(connect_data);
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  AddSSLSocketData();

  base::WeakPtr<SpdySession> session(CreateDefaultSpdySession());

  base::WeakPtr<SpdyStream> stream = CreateStreamSynchronously(
      SPDY_REQUEST_RESPONSE_STREAM, session, url_, LOWEST, NetLogWithSource());
  ASSERT_TRUE(stream);
  EXPECT_EQ(kDefaultUrl, stream->url().spec());

  StreamDelegateDoNothing delegate(stream);
  stream->SetDelegate(&delegate);

  spdy::Http2HeaderBlock headers(
      spdy_util_.ConstructGetHeaderBlock(kDefaultUrl));
  EXPECT_EQ(ERR_IO_PENDING, stream->SendRequestHeaders(std::move(headers),
                                                       NO_MORE_DATA_TO_SEND));

  EXPECT_THAT(delegate.WaitForClose(), IsError(ERR_HTTP2_PROTOCOL_ERROR));

  // Finish async network reads and writes.
  base::RunLoop().RunUntilIdle();

  EXPECT_TRUE(data.AllWriteDataConsumed());
  EXPECT_TRUE(data.AllReadDataConsumed());
}

TEST_F(SpdyStreamTest, HeadersMustHaveStatusOnPushedStream) {
  spdy::SpdySerializedFrame req(
      spdy_util_.ConstructSpdyGet(nullptr, 0, 1, LOWEST));
  AddWrite(req);

  spdy::SpdySerializedFrame reply(
      spdy_util_.ConstructSpdyGetReply(nullptr, 0, 1));
  AddRead(reply);

  spdy::SpdySerializedFrame push_promise(spdy_util_.ConstructSpdyPushPromise(
      1, 2, spdy_util_.ConstructGetHeaderBlock(kPushUrl)));
  AddRead(push_promise);

  spdy::SpdySerializedFrame priority(
      spdy_util_.ConstructSpdyPriority(2, 1, IDLE, true));
  AddWrite(priority);

  // Response headers without ":status" header field: protocol error.
  spdy::Http2HeaderBlock header_block_without_status;
  header_block_without_status[spdy::kHttp2MethodHeader] = "GET";
  header_block_without_status[spdy::kHttp2AuthorityHeader] = "www.example.org";
  header_block_without_status[spdy::kHttp2SchemeHeader] = "https";
  header_block_without_status[spdy::kHttp2PathHeader] = "/";
  spdy::SpdySerializedFrame pushed_reply(
      spdy_util_.ConstructSpdyReply(2, std::move(header_block_without_status)));
  AddRead(pushed_reply);

  spdy::SpdySerializedFrame rst(
      spdy_util_.ConstructSpdyRstStream(2, spdy::ERROR_CODE_PROTOCOL_ERROR));
  AddWrite(rst);

  spdy::SpdySerializedFrame body(
      spdy_util_.ConstructSpdyDataFrame(1, kPostBodyStringPiece, true));
  AddRead(body);

  AddReadEOF();

  SequencedSocketData data(GetReads(), GetWrites());
  MockConnect connect_data(SYNCHRONOUS, OK);
  data.set_connect_data(connect_data);
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  AddSSLSocketData();

  base::WeakPtr<SpdySession> session(CreateDefaultSpdySession());

  base::WeakPtr<SpdyStream> stream = CreateStreamSynchronously(
      SPDY_REQUEST_RESPONSE_STREAM, session, url_, LOWEST, NetLogWithSource());
  ASSERT_TRUE(stream);
  EXPECT_EQ(kDefaultUrl, stream->url().spec());

  StreamDelegateDoNothing delegate(stream);
  stream->SetDelegate(&delegate);

  spdy::Http2HeaderBlock headers(
      spdy_util_.ConstructGetHeaderBlock(kDefaultUrl));
  EXPECT_THAT(
      stream->SendRequestHeaders(std::move(headers), NO_MORE_DATA_TO_SEND),
      IsError(ERR_IO_PENDING));

  EXPECT_THAT(delegate.WaitForClose(), IsOk());
  EXPECT_EQ("200", delegate.GetResponseHeaderValue(spdy::kHttp2StatusHeader));
  EXPECT_EQ(std::string(kPostBody, kPostBodyLength),
            delegate.TakeReceivedData());

  // Finish async network reads and writes.
  base::RunLoop().RunUntilIdle();

  EXPECT_TRUE(data.AllWriteDataConsumed());
  EXPECT_TRUE(data.AllReadDataConsumed());
}

TEST_F(SpdyStreamTest, HeadersMustPreceedData) {
  spdy::SpdySerializedFrame req(
      spdy_util_.ConstructSpdyGet(nullptr, 0, 1, LOWEST));
  AddWrite(req);

  // Response body not preceeded by headers: protocol error.
  spdy::SpdySerializedFrame body(
      spdy_util_.ConstructSpdyDataFrame(1, kPostBodyStringPiece, true));
  AddRead(body);

  spdy::SpdySerializedFrame rst(
      spdy_util_.ConstructSpdyRstStream(1, spdy::ERROR_CODE_PROTOCOL_ERROR));
  AddWrite(rst);

  AddReadEOF();

  SequencedSocketData data(GetReads(), GetWrites());
  MockConnect connect_data(SYNCHRONOUS, OK);
  data.set_connect_data(connect_data);
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  AddSSLSocketData();

  base::WeakPtr<SpdySession> session(CreateDefaultSpdySession());

  base::WeakPtr<SpdyStream> stream = CreateStreamSynchronously(
      SPDY_REQUEST_RESPONSE_STREAM, session, url_, LOWEST, NetLogWithSource());
  ASSERT_TRUE(stream);
  EXPECT_EQ(kDefaultUrl, stream->url().spec());

  StreamDelegateDoNothing delegate(stream);
  stream->SetDelegate(&delegate);

  spdy::Http2HeaderBlock headers(
      spdy_util_.ConstructGetHeaderBlock(kDefaultUrl));
  EXPECT_EQ(ERR_IO_PENDING, stream->SendRequestHeaders(std::move(headers),
                                                       NO_MORE_DATA_TO_SEND));

  EXPECT_THAT(delegate.WaitForClose(), IsError(ERR_HTTP2_PROTOCOL_ERROR));
}

TEST_F(SpdyStreamTest, HeadersMustPreceedDataOnPushedStream) {
  spdy::SpdySerializedFrame req(
      spdy_util_.ConstructSpdyGet(nullptr, 0, 1, LOWEST));
  AddWrite(req);

  spdy::SpdySerializedFrame reply(
      spdy_util_.ConstructSpdyGetReply(nullptr, 0, 1));
  AddRead(reply);

  spdy::SpdySerializedFrame push_promise(spdy_util_.ConstructSpdyPushPromise(
      1, 2, spdy_util_.ConstructGetHeaderBlock(kPushUrl)));
  AddRead(push_promise);

  spdy::SpdySerializedFrame priority(
      spdy_util_.ConstructSpdyPriority(2, 1, IDLE, true));
  AddWrite(priority);

  spdy::SpdySerializedFrame pushed_body(
      spdy_util_.ConstructSpdyDataFrame(2, kPostBodyStringPiece, true));
  AddRead(pushed_body);

  spdy::SpdySerializedFrame rst(
      spdy_util_.ConstructSpdyRstStream(2, spdy::ERROR_CODE_PROTOCOL_ERROR));
  AddWrite(rst);

  spdy::SpdySerializedFrame body(
      spdy_util_.ConstructSpdyDataFrame(1, kPostBodyStringPiece, true));
  AddRead(body);

  AddReadEOF();

  SequencedSocketData data(GetReads(), GetWrites());
  MockConnect connect_data(SYNCHRONOUS, OK);
  data.set_connect_data(connect_data);
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  AddSSLSocketData();

  base::WeakPtr<SpdySession> session(CreateDefaultSpdySession());

  base::WeakPtr<SpdyStream> stream = CreateStreamSynchronously(
      SPDY_REQUEST_RESPONSE_STREAM, session, url_, LOWEST, NetLogWithSource());
  ASSERT_TRUE(stream);
  EXPECT_EQ(kDefaultUrl, stream->url().spec());

  StreamDelegateDoNothing delegate(stream);
  stream->SetDelegate(&delegate);

  spdy::Http2HeaderBlock headers(
      spdy_util_.ConstructGetHeaderBlock(kDefaultUrl));
  EXPECT_THAT(
      stream->SendRequestHeaders(std::move(headers), NO_MORE_DATA_TO_SEND),
      IsError(ERR_IO_PENDING));

  EXPECT_THAT(delegate.WaitForClose(), IsOk());
  EXPECT_EQ("200", delegate.GetResponseHeaderValue(spdy::kHttp2StatusHeader));
  EXPECT_EQ(std::string(kPostBody, kPostBodyLength),
            delegate.TakeReceivedData());

  // Finish async network reads and writes.
  base::RunLoop().RunUntilIdle();

  EXPECT_TRUE(data.AllWriteDataConsumed());
  EXPECT_TRUE(data.AllReadDataConsumed());
}

TEST_F(SpdyStreamTest, TrailersMustNotFollowTrailers) {
  spdy::SpdySerializedFrame req(
      spdy_util_.ConstructSpdyGet(nullptr, 0, 1, LOWEST));
  AddWrite(req);

  spdy::SpdySerializedFrame reply(
      spdy_util_.ConstructSpdyGetReply(nullptr, 0, 1));
  AddRead(reply);

  spdy::SpdySerializedFrame body(
      spdy_util_.ConstructSpdyDataFrame(1, kPostBodyStringPiece, false));
  AddRead(body);

  spdy::Http2HeaderBlock trailers_block;
  trailers_block["foo"] = "bar";
  spdy::SpdySerializedFrame first_trailers(
      spdy_util_.ConstructSpdyResponseHeaders(1, std::move(trailers_block),
                                              false));
  AddRead(first_trailers);

  // Trailers following trailers: procotol error.
  spdy::SpdySerializedFrame second_trailers(
      spdy_util_.ConstructSpdyResponseHeaders(1, std::move(trailers_block),
                                              true));
  AddRead(second_trailers);

  spdy::SpdySerializedFrame rst(
      spdy_util_.ConstructSpdyRstStream(1, spdy::ERROR_CODE_PROTOCOL_ERROR));
  AddWrite(rst);

  AddReadEOF();

  SequencedSocketData data(GetReads(), GetWrites());
  MockConnect connect_data(SYNCHRONOUS, OK);
  data.set_connect_data(connect_data);
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  AddSSLSocketData();

  base::WeakPtr<SpdySession> session(CreateDefaultSpdySession());

  base::WeakPtr<SpdyStream> stream = CreateStreamSynchronously(
      SPDY_REQUEST_RESPONSE_STREAM, session, url_, LOWEST, NetLogWithSource());
  ASSERT_TRUE(stream);
  EXPECT_EQ(kDefaultUrl, stream->url().spec());

  StreamDelegateDoNothing delegate(stream);
  stream->SetDelegate(&delegate);

  spdy::Http2HeaderBlock headers(
      spdy_util_.ConstructGetHeaderBlock(kDefaultUrl));
  EXPECT_EQ(ERR_IO_PENDING, stream->SendRequestHeaders(std::move(headers),
                                                       NO_MORE_DATA_TO_SEND));

  EXPECT_THAT(delegate.WaitForClose(), IsError(ERR_HTTP2_PROTOCOL_ERROR));

  // Finish async network reads and writes.
  base::RunLoop().RunUntilIdle();

  EXPECT_TRUE(data.AllWriteDataConsumed());
  EXPECT_TRUE(data.AllReadDataConsumed());
}

TEST_F(SpdyStreamTest, DataMustNotFollowTrailers) {
  spdy::SpdySerializedFrame req(
      spdy_util_.ConstructSpdyGet(nullptr, 0, 1, LOWEST));
  AddWrite(req);

  spdy::SpdySerializedFrame reply(
      spdy_util_.ConstructSpdyGetReply(nullptr, 0, 1));
  AddRead(reply);

  spdy::SpdySerializedFrame body(
      spdy_util_.ConstructSpdyDataFrame(1, kPostBodyStringPiece, false));
  AddRead(body);

  spdy::Http2HeaderBlock trailers_block;
  trailers_block["foo"] = "bar";
  spdy::SpdySerializedFrame trailers(spdy_util_.ConstructSpdyResponseHeaders(
      1, std::move(trailers_block), false));
  AddRead(trailers);

  // DATA frame following trailers: protocol error.
  AddRead(body);

  spdy::SpdySerializedFrame rst(
      spdy_util_.ConstructSpdyRstStream(1, spdy::ERROR_CODE_PROTOCOL_ERROR));
  AddWrite(rst);

  AddReadEOF();

  SequencedSocketData data(GetReads(), GetWrites());
  MockConnect connect_data(SYNCHRONOUS, OK);
  data.set_connect_data(connect_data);
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  AddSSLSocketData();

  base::WeakPtr<SpdySession> session(CreateDefaultSpdySession());

  base::WeakPtr<SpdyStream> stream = CreateStreamSynchronously(
      SPDY_REQUEST_RESPONSE_STREAM, session, url_, LOWEST, NetLogWithSource());
  ASSERT_TRUE(stream);
  EXPECT_EQ(kDefaultUrl, stream->url().spec());

  StreamDelegateDoNothing delegate(stream);
  stream->SetDelegate(&delegate);

  spdy::Http2HeaderBlock headers(
      spdy_util_.ConstructGetHeaderBlock(kDefaultUrl));
  EXPECT_EQ(ERR_IO_PENDING, stream->SendRequestHeaders(std::move(headers),
                                                       NO_MORE_DATA_TO_SEND));

  EXPECT_THAT(delegate.WaitForClose(), IsError(ERR_HTTP2_PROTOCOL_ERROR));

  // Finish async network reads and writes.
  base::RunLoop().RunUntilIdle();

  EXPECT_TRUE(data.AllWriteDataConsumed());
  EXPECT_TRUE(data.AllReadDataConsumed());
}

class SpdyStreamTestWithMockClock : public SpdyStreamTest {
 public:
  SpdyStreamTestWithMockClock()
      : SpdyStreamTest(base::test::TaskEnvironment::TimeSource::MOCK_TIME) {}

  void Initialize() {
    // Set up the sequenced socket data.
    data_ = std::make_unique<SequencedSocketData>(GetReads(), GetWrites());
    MockConnect connect_data(SYNCHRONOUS, OK);
    data_->set_connect_data(connect_data);
    session_deps_.socket_factory->AddSocketDataProvider(data_.get());

    AddSSLSocketData();

    // Set up the SPDY stream.
    base::WeakPtr<SpdySession> session(CreateDefaultSpdySession());
    stream_ = CreateStreamSynchronously(SPDY_REQUEST_RESPONSE_STREAM, session,
                                        url_, LOWEST, NetLogWithSource());
    ASSERT_TRUE(stream_);
    EXPECT_EQ(kDefaultUrl, stream_->url().spec());

    DCHECK(!delegate_);
    delegate_ = std::make_unique<StreamDelegateDoNothing>(stream_);
    stream_->SetDelegate(delegate_.get());
  }

  void RunUntilNextPause() {
    if (data_->IsPaused())
      data_->Resume();
    data_->RunUntilPaused();
  }

  int RunUntilClose() {
    if (data_->IsPaused())
      data_->Resume();
    return delegate_->WaitForClose();
  }

  SequencedSocketData& data() { return *data_; }
  base::WeakPtr<SpdyStream> stream() { return stream_; }
  StreamDelegateDoNothing& delegate() { return *delegate_; }

 private:
  std::unique_ptr<SequencedSocketData> data_;
  base::WeakPtr<SpdyStream> stream_;
  std::unique_ptr<StreamDelegateDoNothing> delegate_;
};

// Test that the response start time is recorded for non-informational response.
TEST_F(SpdyStreamTestWithMockClock, NonInformationalResponseStart) {
  // Set up the request.
  spdy::SpdySerializedFrame req(
      spdy_util_.ConstructSpdyGet(nullptr, 0, 1, LOWEST));
  AddWrite(req);

  // Set up the response headers.
  spdy::SpdySerializedFrame reply(
      spdy_util_.ConstructSpdyGetReply(nullptr, 0, 1));
  // Separate the headers into 2 fragments and add pauses between the fragments
  // so that the test runner can advance the mock clock to test timing
  // information.
  AddMockRead(ReadFrameExceptForLastByte(reply));
  AddReadPause();
  AddMockRead(LastByteOfReadFrame(reply));
  AddReadPause();

  // Set up the response body.
  spdy::SpdySerializedFrame body(
      spdy_util_.ConstructSpdyDataFrame(1, kPostBodyStringPiece, true));
  AddRead(body);
  AddReadEOF();

  // Set up the sequenced socket data and the spdy stream.
  Initialize();

  // Send a request.
  spdy::Http2HeaderBlock headers(
      spdy_util_.ConstructGetHeaderBlock(kDefaultUrl));
  EXPECT_EQ(ERR_IO_PENDING, stream()->SendRequestHeaders(std::move(headers),
                                                         NO_MORE_DATA_TO_SEND));
  AdvanceClock(base::TimeDelta::FromSeconds(1));

  // The receive headers start time should be captured at this time.
  base::TimeTicks expected_receive_headers_start_time = base::TimeTicks::Now();

  // Read the first header fragment.
  RunUntilNextPause();
  AdvanceClock(base::TimeDelta::FromSeconds(1));
  // Read the second header fragment.
  RunUntilNextPause();
  AdvanceClock(base::TimeDelta::FromSeconds(1));
  EXPECT_EQ("200", delegate().GetResponseHeaderValue(spdy::kHttp2StatusHeader));

  // Read the response body.
  EXPECT_THAT(RunUntilClose(), IsOk());
  EXPECT_EQ(std::string(kPostBody, kPostBodyLength),
            delegate().TakeReceivedData());

  // Finish async network reads and writes.
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(data().AllWriteDataConsumed());
  EXPECT_TRUE(data().AllReadDataConsumed());

  // No informational responses were served. The response start time should be
  // equal to the non-informational response start time.
  const LoadTimingInfo& load_timing_info = delegate().GetLoadTimingInfo();
  EXPECT_EQ(load_timing_info.receive_headers_start,
            expected_receive_headers_start_time);
  EXPECT_EQ(load_timing_info.receive_non_informational_headers_start,
            expected_receive_headers_start_time);
}

TEST_F(SpdyStreamTestWithMockClock, InformationalHeaders) {
  // Set up the request.
  spdy::SpdySerializedFrame req(
      spdy_util_.ConstructSpdyGet(nullptr, 0, 1, LOWEST));
  AddWrite(req);

  // Set up the informational response headers.
  spdy::Http2HeaderBlock informational_headers;
  informational_headers[":status"] = "100";
  spdy::SpdySerializedFrame informational_response(
      spdy_util_.ConstructSpdyResponseHeaders(
          1, std::move(informational_headers), false));
  // Separate the headers into 2 fragments and add pauses between the fragments
  // so that the test runner can advance the mock clock to test timing
  // information.
  AddMockRead(ReadFrameExceptForLastByte(informational_response));
  AddReadPause();
  AddMockRead(LastByteOfReadFrame(informational_response));
  AddReadPause();

  // Set up the non-informational response headers and body.
  spdy::SpdySerializedFrame reply(
      spdy_util_.ConstructSpdyGetReply(nullptr, 0, 1));
  AddRead(reply);
  AddReadPause();
  spdy::SpdySerializedFrame body(
      spdy_util_.ConstructSpdyDataFrame(1, kPostBodyStringPiece, true));
  AddRead(body);
  AddReadEOF();

  // Set up the sequenced socket data and the spdy stream.
  Initialize();

  // Send a request.
  spdy::Http2HeaderBlock headers(
      spdy_util_.ConstructGetHeaderBlock(kDefaultUrl));
  EXPECT_EQ(ERR_IO_PENDING, stream()->SendRequestHeaders(std::move(headers),
                                                         NO_MORE_DATA_TO_SEND));
  AdvanceClock(base::TimeDelta::FromSeconds(1));

  // The receive headers start time should be captured at this time.
  base::TimeTicks expected_receive_headers_start_time = base::TimeTicks::Now();

  // Read the first header fragment of the informational response.
  RunUntilNextPause();
  AdvanceClock(base::TimeDelta::FromSeconds(1));
  // Read the second header fragment of the informational response.
  RunUntilNextPause();
  AdvanceClock(base::TimeDelta::FromSeconds(1));
  // We don't check the status code of the informational headers here because
  // SpdyStream doesn't propagate it to the delegate.

  // The receive non-informational headers start time should be captured at this
  // time.
  base::TimeTicks expected_receive_non_informational_headers_start_time =
      base::TimeTicks::Now();

  // Read the non-informational response headers.
  RunUntilNextPause();
  AdvanceClock(base::TimeDelta::FromSeconds(1));
  EXPECT_EQ("200", delegate().GetResponseHeaderValue(spdy::kHttp2StatusHeader));

  // Read the response body.
  EXPECT_THAT(RunUntilClose(), IsOk());
  EXPECT_EQ(std::string(kPostBody, kPostBodyLength),
            delegate().TakeReceivedData());

  // Finish async network reads and writes.
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(data().AllWriteDataConsumed());
  EXPECT_TRUE(data().AllReadDataConsumed());

  const LoadTimingInfo& load_timing_info = delegate().GetLoadTimingInfo();
  // The response start time should be captured at the time the first header
  // fragment of the informational response is received.
  EXPECT_EQ(load_timing_info.receive_headers_start,
            expected_receive_headers_start_time);
  // The non-informational response start time should be captured at the time
  // the first header fragment of the non-informational response is received.
  EXPECT_EQ(load_timing_info.receive_non_informational_headers_start,
            expected_receive_non_informational_headers_start_time);
  // The first response start time should be earlier than the non-informational
  // response start time.
  EXPECT_LT(load_timing_info.receive_headers_start,
            load_timing_info.receive_non_informational_headers_start);
}

// Tests that timing information of 103 Eary Hints responses are collected and
// callbacks are called as expected.
TEST_F(SpdyStreamTestWithMockClock, EarlyHints) {
  // Set up the request.
  spdy::SpdySerializedFrame req(
      spdy_util_.ConstructSpdyGet(nullptr, 0, 1, LOWEST));
  AddWrite(req);

  // Set up two early hints response headers.
  const char kLinkHeaderValue1[] = "</image.jpg>; rel=preload; as=image";
  spdy::Http2HeaderBlock informational_headers1;
  informational_headers1[":status"] = "103";
  informational_headers1["link"] = kLinkHeaderValue1;
  spdy::SpdySerializedFrame informational_response1(
      spdy_util_.ConstructSpdyResponseHeaders(
          1, std::move(informational_headers1), false));

  const char kLinkHeaderValue2[] = "</style.css>; rel=preload; as=stylesheet";
  spdy::Http2HeaderBlock informational_headers2;
  informational_headers2[":status"] = "103";
  informational_headers2["link"] = kLinkHeaderValue2;
  spdy::SpdySerializedFrame informational_response2(
      spdy_util_.ConstructSpdyResponseHeaders(
          1, std::move(informational_headers2), false));

  // Add the headers to make sure that multiple informational responses don't
  // confuse the timing information.
  const int kNumberOfInformationalResponses = 2;
  // Separate the headers into 2 fragments and add pauses between the
  // fragments so that the test runner can advance the mock clock to test
  // timing information.
  AddMockRead(ReadFrameExceptForLastByte(informational_response1));
  AddReadPause();
  AddMockRead(LastByteOfReadFrame(informational_response1));
  AddReadPause();

  AddMockRead(ReadFrameExceptForLastByte(informational_response2));
  AddReadPause();
  AddMockRead(LastByteOfReadFrame(informational_response2));
  AddReadPause();

  // Set up the non-informational response headers and body.
  spdy::SpdySerializedFrame reply(
      spdy_util_.ConstructSpdyGetReply(nullptr, 0, 1));
  AddRead(reply);
  AddReadPause();
  spdy::SpdySerializedFrame body(
      spdy_util_.ConstructSpdyDataFrame(1, kPostBodyStringPiece, true));
  AddRead(body);
  AddReadEOF();

  // Set up the sequenced socket data and the spdy stream.
  Initialize();

  // Send a request.
  spdy::Http2HeaderBlock headers(
      spdy_util_.ConstructGetHeaderBlock(kDefaultUrl));
  EXPECT_EQ(ERR_IO_PENDING, stream()->SendRequestHeaders(std::move(headers),
                                                         NO_MORE_DATA_TO_SEND));
  AdvanceClock(base::TimeDelta::FromSeconds(1));

  // The receive headers start time should be captured at this time.
  base::TimeTicks expected_receive_headers_start_time = base::TimeTicks::Now();

  // Read the header fragments of the informational responses.
  for (int i = 0; i < kNumberOfInformationalResponses; ++i) {
    RunUntilNextPause();
    AdvanceClock(base::TimeDelta::FromSeconds(1));
    RunUntilNextPause();
    AdvanceClock(base::TimeDelta::FromSeconds(1));
  }

  // Check the callback was called twice with 103 status code.
  const std::vector<spdy::Http2HeaderBlock>& early_hints =
      delegate().early_hints();
  EXPECT_EQ(early_hints.size(),
            static_cast<size_t>(kNumberOfInformationalResponses));
  {
    const spdy::Http2HeaderBlock& hint = delegate().early_hints()[0];
    spdy::Http2HeaderBlock::const_iterator status_iterator =
        hint.find(spdy::kHttp2StatusHeader);
    ASSERT_TRUE(status_iterator != hint.end());
    EXPECT_EQ(status_iterator->second, "103");

    spdy::Http2HeaderBlock::const_iterator link_header_iterator =
        hint.find("link");
    ASSERT_TRUE(link_header_iterator != hint.end());
    EXPECT_EQ(link_header_iterator->second, kLinkHeaderValue1);
  }
  {
    const spdy::Http2HeaderBlock& hint = delegate().early_hints()[1];
    spdy::Http2HeaderBlock::const_iterator status_iterator =
        hint.find(spdy::kHttp2StatusHeader);
    ASSERT_TRUE(status_iterator != hint.end());
    EXPECT_EQ(status_iterator->second, "103");

    spdy::Http2HeaderBlock::const_iterator link_header_iterator =
        hint.find("link");
    ASSERT_TRUE(link_header_iterator != hint.end());
    EXPECT_EQ(link_header_iterator->second, kLinkHeaderValue2);
  }

  // The receive non-informational headers start time should be captured at this
  // time.
  base::TimeTicks expected_receive_non_informational_headers_start_time =
      base::TimeTicks::Now();

  // Read the non-informational response headers.
  RunUntilNextPause();
  AdvanceClock(base::TimeDelta::FromSeconds(1));
  EXPECT_EQ("200", delegate().GetResponseHeaderValue(spdy::kHttp2StatusHeader));

  // Read the response body.
  EXPECT_THAT(RunUntilClose(), IsOk());
  EXPECT_EQ(std::string(kPostBody, kPostBodyLength),
            delegate().TakeReceivedData());

  // Finish async network reads and writes.
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(data().AllWriteDataConsumed());
  EXPECT_TRUE(data().AllReadDataConsumed());

  const LoadTimingInfo& load_timing_info = delegate().GetLoadTimingInfo();
  // The response start time should be captured at the time the first header
  // fragment of the first informational response is received.
  EXPECT_EQ(load_timing_info.receive_headers_start,
            expected_receive_headers_start_time);
  // The first early hints time should be recorded as well.
  EXPECT_EQ(load_timing_info.first_early_hints_time,
            expected_receive_headers_start_time);
  // The non-informational response start time should be captured at the time
  // the first header fragment of the non-informational response is received.
  EXPECT_EQ(load_timing_info.receive_non_informational_headers_start,
            expected_receive_non_informational_headers_start_time);
  // The response start time should be earlier than the non-informational
  // response start time.
  EXPECT_LT(load_timing_info.receive_headers_start,
            load_timing_info.receive_non_informational_headers_start);
}

TEST_F(SpdyStreamTest, StatusMustBeNumber) {
  spdy::SpdySerializedFrame req(
      spdy_util_.ConstructSpdyGet(nullptr, 0, 1, LOWEST));
  AddWrite(req);

  spdy::Http2HeaderBlock incorrect_headers;
  incorrect_headers[":status"] = "nan";
  spdy::SpdySerializedFrame reply(spdy_util_.ConstructSpdyResponseHeaders(
      1, std::move(incorrect_headers), false));
  AddRead(reply);

  spdy::SpdySerializedFrame rst(
      spdy_util_.ConstructSpdyRstStream(1, spdy::ERROR_CODE_PROTOCOL_ERROR));
  AddWrite(rst);

  AddReadEOF();

  SequencedSocketData data(GetReads(), GetWrites());
  MockConnect connect_data(SYNCHRONOUS, OK);
  data.set_connect_data(connect_data);
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  AddSSLSocketData();

  base::WeakPtr<SpdySession> session(CreateDefaultSpdySession());

  base::WeakPtr<SpdyStream> stream = CreateStreamSynchronously(
      SPDY_REQUEST_RESPONSE_STREAM, session, url_, LOWEST, NetLogWithSource());
  ASSERT_TRUE(stream);
  EXPECT_EQ(kDefaultUrl, stream->url().spec());

  StreamDelegateDoNothing delegate(stream);
  stream->SetDelegate(&delegate);

  spdy::Http2HeaderBlock headers(
      spdy_util_.ConstructGetHeaderBlock(kDefaultUrl));
  EXPECT_EQ(ERR_IO_PENDING, stream->SendRequestHeaders(std::move(headers),
                                                       NO_MORE_DATA_TO_SEND));

  EXPECT_THAT(delegate.WaitForClose(), IsError(ERR_HTTP2_PROTOCOL_ERROR));

  // Finish async network reads and writes.
  base::RunLoop().RunUntilIdle();

  EXPECT_TRUE(data.AllWriteDataConsumed());
  EXPECT_TRUE(data.AllReadDataConsumed());
}

TEST_F(SpdyStreamTest, StatusCannotHaveExtraText) {
  spdy::SpdySerializedFrame req(
      spdy_util_.ConstructSpdyGet(nullptr, 0, 1, LOWEST));
  AddWrite(req);

  spdy::Http2HeaderBlock headers_with_status_text;
  headers_with_status_text[":status"] =
      "200 Some random extra text describing status";
  spdy::SpdySerializedFrame reply(spdy_util_.ConstructSpdyResponseHeaders(
      1, std::move(headers_with_status_text), false));
  AddRead(reply);

  spdy::SpdySerializedFrame body(
      spdy_util_.ConstructSpdyDataFrame(1, kPostBodyStringPiece, true));
  AddRead(body);

  spdy::SpdySerializedFrame rst(
      spdy_util_.ConstructSpdyRstStream(1, spdy::ERROR_CODE_PROTOCOL_ERROR));
  AddWrite(rst);

  AddReadEOF();

  SequencedSocketData data(GetReads(), GetWrites());
  MockConnect connect_data(SYNCHRONOUS, OK);
  data.set_connect_data(connect_data);
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  AddSSLSocketData();

  base::WeakPtr<SpdySession> session(CreateDefaultSpdySession());

  base::WeakPtr<SpdyStream> stream = CreateStreamSynchronously(
      SPDY_REQUEST_RESPONSE_STREAM, session, url_, LOWEST, NetLogWithSource());
  ASSERT_TRUE(stream);
  EXPECT_EQ(kDefaultUrl, stream->url().spec());

  StreamDelegateDoNothing delegate(stream);
  stream->SetDelegate(&delegate);

  spdy::Http2HeaderBlock headers(
      spdy_util_.ConstructGetHeaderBlock(kDefaultUrl));
  EXPECT_EQ(ERR_IO_PENDING, stream->SendRequestHeaders(std::move(headers),
                                                       NO_MORE_DATA_TO_SEND));

  EXPECT_THAT(delegate.WaitForClose(), IsError(ERR_HTTP2_PROTOCOL_ERROR));

  // Finish async network reads and writes.
  base::RunLoop().RunUntilIdle();

  EXPECT_TRUE(data.AllWriteDataConsumed());
  EXPECT_TRUE(data.AllReadDataConsumed());
}

TEST_F(SpdyStreamTest, StatusMustBePresent) {
  spdy::SpdySerializedFrame req(
      spdy_util_.ConstructSpdyGet(nullptr, 0, 1, LOWEST));
  AddWrite(req);

  spdy::Http2HeaderBlock headers_without_status;
  spdy::SpdySerializedFrame reply(spdy_util_.ConstructSpdyResponseHeaders(
      1, std::move(headers_without_status), false));
  AddRead(reply);

  spdy::SpdySerializedFrame body(
      spdy_util_.ConstructSpdyDataFrame(1, kPostBodyStringPiece, true));
  AddRead(body);

  spdy::SpdySerializedFrame rst(
      spdy_util_.ConstructSpdyRstStream(1, spdy::ERROR_CODE_PROTOCOL_ERROR));
  AddWrite(rst);

  AddReadEOF();

  SequencedSocketData data(GetReads(), GetWrites());
  MockConnect connect_data(SYNCHRONOUS, OK);
  data.set_connect_data(connect_data);
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  AddSSLSocketData();

  base::WeakPtr<SpdySession> session(CreateDefaultSpdySession());

  base::WeakPtr<SpdyStream> stream = CreateStreamSynchronously(
      SPDY_REQUEST_RESPONSE_STREAM, session, url_, LOWEST, NetLogWithSource());
  ASSERT_TRUE(stream);
  EXPECT_EQ(kDefaultUrl, stream->url().spec());

  StreamDelegateDoNothing delegate(stream);
  stream->SetDelegate(&delegate);

  spdy::Http2HeaderBlock headers(
      spdy_util_.ConstructGetHeaderBlock(kDefaultUrl));
  EXPECT_EQ(ERR_IO_PENDING, stream->SendRequestHeaders(std::move(headers),
                                                       NO_MORE_DATA_TO_SEND));

  EXPECT_THAT(delegate.WaitForClose(), IsError(ERR_HTTP2_PROTOCOL_ERROR));

  // Finish async network reads and writes.
  base::RunLoop().RunUntilIdle();

  EXPECT_TRUE(data.AllWriteDataConsumed());
  EXPECT_TRUE(data.AllReadDataConsumed());
}

// Call IncreaseSendWindowSize on a stream with a large enough delta to overflow
// an int32_t. The SpdyStream should handle that case gracefully.
TEST_F(SpdyStreamTest, IncreaseSendWindowSizeOverflow) {
  spdy::SpdySerializedFrame req(spdy_util_.ConstructSpdyPost(
      kDefaultUrl, 1, kPostBodyLength, LOWEST, nullptr, 0));
  AddWrite(req);

  AddReadPause();

  // Triggered by the overflowing call to IncreaseSendWindowSize
  // below.
  spdy::SpdySerializedFrame rst(spdy_util_.ConstructSpdyRstStream(
      1, spdy::ERROR_CODE_FLOW_CONTROL_ERROR));
  AddWrite(rst);

  AddReadEOF();

  RecordingBoundTestNetLog log;

  SequencedSocketData data(GetReads(), GetWrites());
  MockConnect connect_data(SYNCHRONOUS, OK);
  data.set_connect_data(connect_data);
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  AddSSLSocketData();

  base::WeakPtr<SpdySession> session(CreateDefaultSpdySession());

  base::WeakPtr<SpdyStream> stream = CreateStreamSynchronously(
      SPDY_BIDIRECTIONAL_STREAM, session, url_, LOWEST, log.bound());
  ASSERT_TRUE(stream);
  EXPECT_EQ(kDefaultUrl, stream->url().spec());

  StreamDelegateSendImmediate delegate(stream, kPostBodyStringPiece);
  stream->SetDelegate(&delegate);

  spdy::Http2HeaderBlock headers(
      spdy_util_.ConstructPostHeaderBlock(kDefaultUrl, kPostBodyLength));
  EXPECT_THAT(stream->SendRequestHeaders(std::move(headers), MORE_DATA_TO_SEND),
              IsError(ERR_IO_PENDING));

  data.RunUntilPaused();

  int32_t old_send_window_size = stream->send_window_size();
  ASSERT_GT(old_send_window_size, 0);
  int32_t delta_window_size =
      std::numeric_limits<int32_t>::max() - old_send_window_size + 1;
  stream->IncreaseSendWindowSize(delta_window_size);
  EXPECT_FALSE(stream);

  data.Resume();
  base::RunLoop().RunUntilIdle();

  EXPECT_THAT(delegate.WaitForClose(), IsError(ERR_HTTP2_FLOW_CONTROL_ERROR));
}

// Functions used with
// RunResumeAfterUnstall{RequestResponse,Bidirectional}Test().

void StallStream(const base::WeakPtr<SpdyStream>& stream) {
  // Reduce the send window size to 0 to stall.
  while (stream->send_window_size() > 0) {
    stream->DecreaseSendWindowSize(
        std::min(kMaxSpdyFrameChunkSize, stream->send_window_size()));
  }
}

void IncreaseStreamSendWindowSize(const base::WeakPtr<SpdyStream>& stream,
                                  int32_t delta_window_size) {
  EXPECT_TRUE(stream->send_stalled_by_flow_control());
  stream->IncreaseSendWindowSize(delta_window_size);
  EXPECT_FALSE(stream->send_stalled_by_flow_control());
}

void AdjustStreamSendWindowSize(const base::WeakPtr<SpdyStream>& stream,
                                int32_t delta_window_size) {
  // Make sure that negative adjustments are handled properly.
  EXPECT_TRUE(stream->send_stalled_by_flow_control());
  EXPECT_TRUE(stream->AdjustSendWindowSize(-delta_window_size));
  EXPECT_TRUE(stream->send_stalled_by_flow_control());
  EXPECT_TRUE(stream->AdjustSendWindowSize(+delta_window_size));
  EXPECT_TRUE(stream->send_stalled_by_flow_control());
  EXPECT_TRUE(stream->AdjustSendWindowSize(+delta_window_size));
  EXPECT_FALSE(stream->send_stalled_by_flow_control());
}

// Given an unstall function, runs a test to make sure that a
// request/response (i.e., an HTTP-like) stream resumes after a stall
// and unstall.
void SpdyStreamTest::RunResumeAfterUnstallRequestResponseTest(
    UnstallFunction unstall_function) {
  spdy::SpdySerializedFrame req(spdy_util_.ConstructSpdyPost(
      kDefaultUrl, 1, kPostBodyLength, LOWEST, nullptr, 0));
  AddWrite(req);

  spdy::SpdySerializedFrame body(
      spdy_util_.ConstructSpdyDataFrame(1, kPostBodyStringPiece, true));
  AddWrite(body);

  spdy::SpdySerializedFrame resp(
      spdy_util_.ConstructSpdyGetReply(nullptr, 0, 1));
  AddRead(resp);

  AddReadEOF();

  SequencedSocketData data(GetReads(), GetWrites());
  MockConnect connect_data(SYNCHRONOUS, OK);
  data.set_connect_data(connect_data);
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  AddSSLSocketData();

  base::WeakPtr<SpdySession> session(CreateDefaultSpdySession());

  base::WeakPtr<SpdyStream> stream = CreateStreamSynchronously(
      SPDY_REQUEST_RESPONSE_STREAM, session, url_, LOWEST, NetLogWithSource());
  ASSERT_TRUE(stream);
  EXPECT_EQ(kDefaultUrl, stream->url().spec());

  StreamDelegateWithBody delegate(stream, kPostBodyStringPiece);
  stream->SetDelegate(&delegate);

  EXPECT_FALSE(stream->send_stalled_by_flow_control());

  spdy::Http2HeaderBlock headers(
      spdy_util_.ConstructPostHeaderBlock(kDefaultUrl, kPostBodyLength));
  EXPECT_THAT(stream->SendRequestHeaders(std::move(headers), MORE_DATA_TO_SEND),
              IsError(ERR_IO_PENDING));

  StallStream(stream);

  base::RunLoop().RunUntilIdle();

  EXPECT_TRUE(stream->send_stalled_by_flow_control());

  std::move(unstall_function).Run(stream, kPostBodyLength);

  EXPECT_FALSE(stream->send_stalled_by_flow_control());

  EXPECT_THAT(delegate.WaitForClose(), IsError(ERR_CONNECTION_CLOSED));

  EXPECT_TRUE(delegate.send_headers_completed());
  EXPECT_EQ("200", delegate.GetResponseHeaderValue(":status"));
  EXPECT_EQ(std::string(), delegate.TakeReceivedData());
  EXPECT_TRUE(data.AllWriteDataConsumed());
}

TEST_F(SpdyStreamTest, ResumeAfterSendWindowSizeIncreaseRequestResponse) {
  RunResumeAfterUnstallRequestResponseTest(
      base::BindOnce(&IncreaseStreamSendWindowSize));
}

TEST_F(SpdyStreamTest, ResumeAfterSendWindowSizeAdjustRequestResponse) {
  RunResumeAfterUnstallRequestResponseTest(
      base::BindOnce(&AdjustStreamSendWindowSize));
}

// Given an unstall function, runs a test to make sure that a bidirectional
// (i.e., non-HTTP-like) stream resumes after a stall and unstall.
void SpdyStreamTest::RunResumeAfterUnstallBidirectionalTest(
    UnstallFunction unstall_function) {
  spdy::SpdySerializedFrame req(spdy_util_.ConstructSpdyPost(
      kDefaultUrl, 1, kPostBodyLength, LOWEST, nullptr, 0));
  AddWrite(req);

  AddReadPause();

  spdy::SpdySerializedFrame resp(
      spdy_util_.ConstructSpdyGetReply(nullptr, 0, 1));
  AddRead(resp);

  spdy::SpdySerializedFrame msg(
      spdy_util_.ConstructSpdyDataFrame(1, kPostBodyStringPiece, false));
  AddWrite(msg);

  spdy::SpdySerializedFrame echo(
      spdy_util_.ConstructSpdyDataFrame(1, kPostBodyStringPiece, false));
  AddRead(echo);

  AddReadEOF();

  SequencedSocketData data(GetReads(), GetWrites());
  MockConnect connect_data(SYNCHRONOUS, OK);
  data.set_connect_data(connect_data);
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  AddSSLSocketData();

  base::WeakPtr<SpdySession> session(CreateDefaultSpdySession());

  base::WeakPtr<SpdyStream> stream = CreateStreamSynchronously(
      SPDY_BIDIRECTIONAL_STREAM, session, url_, LOWEST, NetLogWithSource());
  ASSERT_TRUE(stream);
  EXPECT_EQ(kDefaultUrl, stream->url().spec());

  StreamDelegateSendImmediate delegate(stream, kPostBodyStringPiece);
  stream->SetDelegate(&delegate);

  spdy::Http2HeaderBlock headers(
      spdy_util_.ConstructPostHeaderBlock(kDefaultUrl, kPostBodyLength));
  EXPECT_THAT(stream->SendRequestHeaders(std::move(headers), MORE_DATA_TO_SEND),
              IsError(ERR_IO_PENDING));

  data.RunUntilPaused();

  EXPECT_FALSE(stream->send_stalled_by_flow_control());

  StallStream(stream);

  data.Resume();
  base::RunLoop().RunUntilIdle();

  EXPECT_TRUE(stream->send_stalled_by_flow_control());

  std::move(unstall_function).Run(stream, kPostBodyLength);

  EXPECT_FALSE(stream->send_stalled_by_flow_control());

  EXPECT_THAT(delegate.WaitForClose(), IsError(ERR_CONNECTION_CLOSED));

  EXPECT_TRUE(delegate.send_headers_completed());
  EXPECT_EQ("200", delegate.GetResponseHeaderValue(":status"));
  EXPECT_EQ(std::string(kPostBody, kPostBodyLength),
            delegate.TakeReceivedData());
  EXPECT_TRUE(data.AllWriteDataConsumed());
}

TEST_F(SpdyStreamTest, ResumeAfterSendWindowSizeIncreaseBidirectional) {
  RunResumeAfterUnstallBidirectionalTest(
      base::BindOnce(&IncreaseStreamSendWindowSize));
}

TEST_F(SpdyStreamTest, ResumeAfterSendWindowSizeAdjustBidirectional) {
  RunResumeAfterUnstallBidirectionalTest(
      base::BindOnce(&AdjustStreamSendWindowSize));
}

// Test calculation of amount of bytes received from network.
TEST_F(SpdyStreamTest, ReceivedBytes) {
  spdy::SpdySerializedFrame req(
      spdy_util_.ConstructSpdyGet(nullptr, 0, 1, LOWEST));
  AddWrite(req);

  AddReadPause();

  spdy::SpdySerializedFrame reply(
      spdy_util_.ConstructSpdyGetReply(nullptr, 0, 1));
  AddRead(reply);

  AddReadPause();

  spdy::SpdySerializedFrame msg(
      spdy_util_.ConstructSpdyDataFrame(1, kPostBodyStringPiece, false));
  AddRead(msg);

  AddReadPause();

  AddReadEOF();

  SequencedSocketData data(GetReads(), GetWrites());
  MockConnect connect_data(SYNCHRONOUS, OK);
  data.set_connect_data(connect_data);
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  AddSSLSocketData();

  base::WeakPtr<SpdySession> session(CreateDefaultSpdySession());

  base::WeakPtr<SpdyStream> stream = CreateStreamSynchronously(
      SPDY_REQUEST_RESPONSE_STREAM, session, url_, LOWEST, NetLogWithSource());
  ASSERT_TRUE(stream);
  EXPECT_EQ(kDefaultUrl, stream->url().spec());

  StreamDelegateDoNothing delegate(stream);
  stream->SetDelegate(&delegate);

  spdy::Http2HeaderBlock headers(
      spdy_util_.ConstructGetHeaderBlock(kDefaultUrl));
  EXPECT_THAT(
      stream->SendRequestHeaders(std::move(headers), NO_MORE_DATA_TO_SEND),
      IsError(ERR_IO_PENDING));

  int64_t reply_frame_len = reply.size();
  int64_t data_header_len = spdy::kDataFrameMinimumSize;
  int64_t data_frame_len = data_header_len + kPostBodyLength;
  int64_t response_len = reply_frame_len + data_frame_len;

  EXPECT_EQ(0, stream->raw_received_bytes());

  // REQUEST
  data.RunUntilPaused();
  EXPECT_EQ(0, stream->raw_received_bytes());

  // REPLY
  data.Resume();
  data.RunUntilPaused();
  EXPECT_EQ(reply_frame_len, stream->raw_received_bytes());

  // DATA
  data.Resume();
  data.RunUntilPaused();
  EXPECT_EQ(response_len, stream->raw_received_bytes());

  // FIN
  data.Resume();
  EXPECT_THAT(delegate.WaitForClose(), IsError(ERR_CONNECTION_CLOSED));
}

// Regression test for https://crbug.com/810763.
TEST_F(SpdyStreamTest, DataOnHalfClosedRemoveStream) {
  spdy::SpdySerializedFrame req(spdy_util_.ConstructSpdyPost(
      kDefaultUrl, 1, kPostBodyLength, LOWEST, nullptr, 0));
  AddWrite(req);

  spdy::Http2HeaderBlock response_headers;
  response_headers[spdy::kHttp2StatusHeader] = "200";
  spdy::SpdySerializedFrame resp(spdy_util_.ConstructSpdyResponseHeaders(
      1, std::move(response_headers), /* fin = */ true));
  AddRead(resp);

  spdy::SpdySerializedFrame data_frame(
      spdy_util_.ConstructSpdyDataFrame(1, kPostBodyStringPiece, true));
  AddRead(data_frame);

  spdy::SpdySerializedFrame rst(
      spdy_util_.ConstructSpdyRstStream(1, spdy::ERROR_CODE_STREAM_CLOSED));
  AddWrite(rst);

  AddReadEOF();

  SequencedSocketData data(GetReads(), GetWrites());
  MockConnect connect_data(SYNCHRONOUS, OK);
  data.set_connect_data(connect_data);
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  AddSSLSocketData();

  base::WeakPtr<SpdySession> session(CreateDefaultSpdySession());

  base::WeakPtr<SpdyStream> stream = CreateStreamSynchronously(
      SPDY_BIDIRECTIONAL_STREAM, session, url_, LOWEST, NetLogWithSource());
  ASSERT_TRUE(stream);
  EXPECT_EQ(kDefaultUrl, stream->url().spec());

  StreamDelegateDoNothing delegate(stream);
  stream->SetDelegate(&delegate);

  spdy::Http2HeaderBlock headers(
      spdy_util_.ConstructPostHeaderBlock(kDefaultUrl, kPostBodyLength));
  EXPECT_THAT(stream->SendRequestHeaders(std::move(headers), MORE_DATA_TO_SEND),
              IsError(ERR_IO_PENDING));

  EXPECT_THAT(delegate.WaitForClose(), IsError(ERR_HTTP2_STREAM_CLOSED));

  base::RunLoop().RunUntilIdle();

  EXPECT_TRUE(data.AllReadDataConsumed());
  EXPECT_TRUE(data.AllWriteDataConsumed());
}

TEST_F(SpdyStreamTest, DelegateIsInformedOfEOF) {
  spdy::SpdySerializedFrame req(spdy_util_.ConstructSpdyPost(
      kDefaultUrl, 1, kPostBodyLength, LOWEST, nullptr, 0));
  AddWrite(req);

  spdy::Http2HeaderBlock response_headers;
  response_headers[spdy::kHttp2StatusHeader] = "200";
  spdy::SpdySerializedFrame resp(spdy_util_.ConstructSpdyResponseHeaders(
      1, std::move(response_headers), /* fin = */ true));
  AddRead(resp);

  spdy::SpdySerializedFrame data_frame(
      spdy_util_.ConstructSpdyDataFrame(1, kPostBodyStringPiece, true));
  AddRead(data_frame);

  spdy::SpdySerializedFrame rst(
      spdy_util_.ConstructSpdyRstStream(1, spdy::ERROR_CODE_STREAM_CLOSED));
  AddWrite(rst);

  AddReadEOF();

  SequencedSocketData data(GetReads(), GetWrites());
  MockConnect connect_data(SYNCHRONOUS, OK);
  data.set_connect_data(connect_data);
  session_deps_.socket_factory->AddSocketDataProvider(&data);

  AddSSLSocketData();

  base::WeakPtr<SpdySession> session(CreateDefaultSpdySession());

  base::WeakPtr<SpdyStream> stream = CreateStreamSynchronously(
      SPDY_BIDIRECTIONAL_STREAM, session, url_, LOWEST, NetLogWithSource());
  ASSERT_TRUE(stream);
  EXPECT_EQ(kDefaultUrl, stream->url().spec());

  StreamDelegateDetectEOF delegate(stream);
  stream->SetDelegate(&delegate);

  spdy::Http2HeaderBlock headers(
      spdy_util_.ConstructPostHeaderBlock(kDefaultUrl, kPostBodyLength));
  EXPECT_THAT(stream->SendRequestHeaders(std::move(headers), MORE_DATA_TO_SEND),
              IsError(ERR_IO_PENDING));

  base::RunLoop().RunUntilIdle();

  EXPECT_TRUE(delegate.eof_detected());

  EXPECT_TRUE(data.AllReadDataConsumed());
  EXPECT_TRUE(data.AllWriteDataConsumed());
}

}  // namespace test

}  // namespace net
