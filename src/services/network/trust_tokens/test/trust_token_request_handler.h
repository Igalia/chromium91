// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SERVICES_NETWORK_TRUST_TOKENS_TEST_TRUST_TOKEN_REQUEST_HANDLER_H_
#define SERVICES_NETWORK_TRUST_TOKENS_TEST_TRUST_TOKEN_REQUEST_HANDLER_H_

#include <set>
#include <string>

#include "base/optional.h"
#include "base/strings/string_piece.h"
#include "base/synchronization/lock.h"
#include "base/time/time.h"
#include "net/http/http_request_headers.h"
#include "services/network/public/mojom/trust_tokens.mojom.h"
#include "url/gurl.h"

namespace network {
namespace test {

struct TrustTokenSignedRequest {
  GURL destination;
  net::HttpRequestHeaders headers;
};

// TrustTokenRequestHandler encapsulates server-side Trust Tokens issuance and
// redemption logic and implements some integrity and correctness checks for
// requests subsequently signed with keys bound to token redemptions.
//
// It's thread-safe so that the methods can be called by test code directly and
// by net::EmbeddedTestServer handlers.
class TrustTokenRequestHandler {
 public:
  struct Options;  // Definition below.
  explicit TrustTokenRequestHandler(Options options);

  // The default constructor uses reasonable default options.
  TrustTokenRequestHandler();

  ~TrustTokenRequestHandler();

  // TODO(davidvc): Provide a way to specify when keys expire.

  // See |Options::client_signing_outcome| below.
  enum class SigningOutcome {
    // Expect a well-formed RR and possibly a Sec-Signature header.
    kSuccess,
    // Expect an empty Sec-Redemption-Record header and no Sec-Signature header.
    kFailure,
  };

  enum class ServerOperationOutcome {
    kExecuteOperationAsNormal,
    kUnconditionalFailure,
  };

  struct Options final {
    Options();
    ~Options();
    Options(const Options&);
    Options& operator=(const Options&);

    // The number of issuance key pairs to provide via key commitment results.
    int num_keys = 1;

    // Specifies whether the client-side signing operation is expected to
    // succeed. Unlike issuance and redemption, clients send signed requests
    // even when the operation failures, but the outcome affects the shape of
    // the expected request.
    SigningOutcome client_signing_outcome = SigningOutcome::kSuccess;

    // The protocol version with which to parameterize the server-side
    // cryptographic logic. We return this value in key commitment results.
    std::string protocol_version = "TrustTokenV2PMB";

    // The key commitment ID.
    int id = 1;

    // The number of tokens to sign per issuance operation; this value is also
    // provided to the client as part of key commitment results.
    int batch_size = 10;

    // If set to |kUnconditionalFailure|, returns a failure response for the
    // corresponding operation even if the operation would have succeeded had
    // the server been operating correctly.
    ServerOperationOutcome issuance_outcome =
        ServerOperationOutcome::kExecuteOperationAsNormal;
    ServerOperationOutcome redemption_outcome =
        ServerOperationOutcome::kExecuteOperationAsNormal;

    // The following two fields specify operating systems on which to specify
    // that the browser should attempt platform-provided trust token issuance
    // instead of sending requests directly to the issuer's server, and the
    // fallback behavior when these operations are unavailable. This information
    // will be included in GetKeyCommitmentRecord's returned commitments.
    std::set<mojom::TrustTokenKeyCommitmentResult::Os>
        specify_platform_issuance_on;
    mojom::TrustTokenKeyCommitmentResult::UnavailableLocalOperationFallback
        unavailable_local_operation_fallback =
            mojom::TrustTokenKeyCommitmentResult::
                UnavailableLocalOperationFallback::kReturnWithError;
  };

  // Updates the handler's options, resetting its internal state.
  void UpdateOptions(Options options);

  // Returns a key commitment record suitable for inserting into a {issuer:
  // commitment} dictionary passed to the network service via
  // NetworkService::SetTrustTokenKeyCommitments. This comprises |num_keys|
  // token verification keys, a protocol version of |protocol_version|, an ID of
  // |id| and  a batch size of |batch_size| (or none if |batch_size| is
  // nullopt).
  std::string GetKeyCommitmentRecord() const;

  // Given a base64-encoded issuance request, processes the
  // request and returns either nullopt (on error) or a base64-encoded response.
  base::Optional<std::string> Issue(base::StringPiece issuance_request);

  // Given a base64-encoded redemption request, processes the
  // request and returns either nullopt (on error) or a base64-encoded response.
  // On success, the response's redemption record will have a lifetime of
  // |kRRLifetime|. We use a ludicrously long lifetime because there's no way
  // to mock time in browser tests, and we don't want the RR expiring
  // unexpectedly.
  //
  // TODO(davidvc): This needs to be expanded to be able to provide
  // RRs that have already expired. (This seems like the easiest way of
  // exercising client-side RR expiry logic in end-to-end tests, because
  // there's no way to fast-forward a clock past an expiry time.)
  static const base::TimeDelta kRrLifetime;
  base::Optional<std::string> Redeem(base::StringPiece redemption_request);

  // Stores a representation of a signed request with the given destination and
  // headers in a manner that can be retrieved for inspection by calling
  // |last_incoming_signed_request|.
  void RecordSignedRequest(const GURL& destination,
                           const net::HttpRequestHeaders& headers);

  // Returns the public key hashes received in prior redemption requests.
  std::set<std::string> hashes_of_redemption_bound_public_keys() const;

  // Returns a structured representation of the last signed request received.
  base::Optional<TrustTokenSignedRequest> last_incoming_signed_request() const;

 private:
  struct Rep;  // Contains state internal to this class's implementation.

  // Guards this class's internal state. This makes sure we're reading writes to
  // the state that occur while handling requests, which takes place off of the
  // main sequence due to how net::EmbeddedTestServer works.
  mutable base::Lock mutex_;
  std::unique_ptr<Rep> rep_ GUARDED_BY(mutex_);
};

}  // namespace test
}  // namespace network

#endif  // SERVICES_NETWORK_TRUST_TOKENS_TEST_TRUST_TOKEN_REQUEST_HANDLER_H_
