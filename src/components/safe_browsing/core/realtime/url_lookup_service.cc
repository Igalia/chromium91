// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/safe_browsing/core/realtime/url_lookup_service.h"

#include "base/base64url.h"
#include "base/metrics/field_trial_params.h"
#include "base/metrics/histogram_functions.h"
#include "base/metrics/histogram_macros.h"
#include "base/strings/strcat.h"
#include "base/strings/string_piece.h"
#include "base/task/post_task.h"
#include "base/time/time.h"
#include "components/prefs/pref_service.h"
#include "components/safe_browsing/buildflags.h"
#include "components/safe_browsing/core/browser/referrer_chain_provider.h"
#include "components/safe_browsing/core/browser/safe_browsing_token_fetcher.h"
#include "components/safe_browsing/core/common/safe_browsing_prefs.h"
#include "components/safe_browsing/core/common/thread_utils.h"
#include "components/safe_browsing/core/db/v4_protocol_manager_util.h"
#include "components/safe_browsing/core/features.h"
#include "components/safe_browsing/core/realtime/policy_engine.h"
#include "net/base/ip_address.h"
#include "net/base/load_flags.h"
#include "net/base/url_util.h"
#include "net/http/http_status_code.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"

namespace {

constexpr char kRealTimeUrlLookupReferrerLengthParam[] =
    "SafeBrowsingRealTimeUrlLookupReferrerLengthParam";
constexpr int kDefaultRealTimeUrlLookupReferrerLength = 2;

}  // namespace

namespace safe_browsing {

RealTimeUrlLookupService::RealTimeUrlLookupService(
    scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory,
    VerdictCacheManager* cache_manager,
    base::RepeatingCallback<ChromeUserPopulation()>
        get_user_population_callback,
    PrefService* pref_service,
    std::unique_ptr<SafeBrowsingTokenFetcher> token_fetcher,
    const ClientConfiguredForTokenFetchesCallback& client_token_config_callback,
    bool is_off_the_record,
    variations::VariationsService* variations_service,
    ReferrerChainProvider* referrer_chain_provider)
    : RealTimeUrlLookupServiceBase(url_loader_factory,
                                   cache_manager,
                                   get_user_population_callback,
                                   referrer_chain_provider),
      pref_service_(pref_service),
      token_fetcher_(std::move(token_fetcher)),
      client_token_config_callback_(client_token_config_callback),
      is_off_the_record_(is_off_the_record),
      variations_(variations_service) {}

void RealTimeUrlLookupService::GetAccessToken(
    const GURL& url,
    RTLookupRequestCallback request_callback,
    RTLookupResponseCallback response_callback) {
  token_fetcher_->Start(
      base::BindOnce(&RealTimeUrlLookupService::OnGetAccessToken,
                     weak_factory_.GetWeakPtr(), url,
                     std::move(request_callback), std::move(response_callback),
                     base::TimeTicks::Now()));
}

void RealTimeUrlLookupService::OnGetAccessToken(
    const GURL& url,
    RTLookupRequestCallback request_callback,
    RTLookupResponseCallback response_callback,
    base::TimeTicks get_token_start_time,
    const std::string& access_token) {
  base::UmaHistogramTimes("SafeBrowsing.RT.GetToken.Time",
                          base::TimeTicks::Now() - get_token_start_time);
  base::UmaHistogramBoolean("SafeBrowsing.RT.HasTokenFromFetcher",
                            !access_token.empty());
  SendRequest(url, access_token, std::move(request_callback),
              std::move(response_callback));
}

RealTimeUrlLookupService::~RealTimeUrlLookupService() {}

bool RealTimeUrlLookupService::CanPerformFullURLLookup() const {
  return RealTimePolicyEngine::CanPerformFullURLLookup(
      pref_service_, is_off_the_record_, variations_);
}

bool RealTimeUrlLookupService::CanPerformFullURLLookupWithToken() const {
  return RealTimePolicyEngine::CanPerformFullURLLookupWithToken(
      pref_service_, is_off_the_record_, client_token_config_callback_,
      variations_);
}

bool RealTimeUrlLookupService::CanAttachReferrerChain() const {
  return base::FeatureList::IsEnabled(kRealTimeUrlLookupReferrerChain);
}

int RealTimeUrlLookupService::GetReferrerUserGestureLimit() const {
  return base::GetFieldTrialParamByFeatureAsInt(
      kRealTimeUrlLookupReferrerChain, kRealTimeUrlLookupReferrerLengthParam,
      kDefaultRealTimeUrlLookupReferrerLength);
}

bool RealTimeUrlLookupService::CanCheckSubresourceURL() const {
  return IsEnhancedProtectionEnabled(*pref_service_);
}

bool RealTimeUrlLookupService::CanCheckSafeBrowsingDb() const {
  // Always return true, because consumer real time URL check only works when
  // safe browsing is enabled.
  return true;
}

void RealTimeUrlLookupService::Shutdown() {
  // Clear state that was potentially bound to the lifetime of other
  // KeyedServices by the embedder.
  token_fetcher_.reset();
  client_token_config_callback_ = ClientConfiguredForTokenFetchesCallback();

  RealTimeUrlLookupServiceBase::Shutdown();
}

GURL RealTimeUrlLookupService::GetRealTimeLookupUrl() const {
  return GURL(
      "https://safebrowsing.google.com/safebrowsing/clientreport/realtime");
}

net::NetworkTrafficAnnotationTag
RealTimeUrlLookupService::GetTrafficAnnotationTag() const {
  return net::DefineNetworkTrafficAnnotation(
      "safe_browsing_realtime_url_lookup",
      R"(
        semantics {
          sender: "Safe Browsing"
          description:
            "When Safe Browsing can't detect that a URL is safe based on its "
            "local database, it sends the top-level URL to Google to verify it "
            "before showing a warning to the user."
          trigger:
            "When a main frame URL fails to match the local hash-prefix "
            "database of known safe URLs and a valid result from a prior "
            "lookup is not already cached, this will be sent."
          data: "The main frame URL that did not match the local safelist."
          destination: GOOGLE_OWNED_SERVICE
        }
        policy {
          cookies_allowed: YES
          cookies_store: "Safe Browsing cookie store"
          setting:
            "Users can disable Safe Browsing real time URL checks by "
            "unchecking 'Protect you and your device from dangerous sites' in "
            "Chromium settings under Privacy, or by unchecking 'Make searches "
            "and browsing better (Sends URLs of pages you visit to Google)' in "
            "Chromium settings under Privacy."
          chrome_policy {
            UrlKeyedAnonymizedDataCollectionEnabled {
              policy_options {mode: MANDATORY}
              UrlKeyedAnonymizedDataCollectionEnabled: false
            }
          }
        })");
}

base::Optional<std::string> RealTimeUrlLookupService::GetDMTokenString() const {
  // DM token should only be set for enterprise requests.
  return base::nullopt;
}

std::string RealTimeUrlLookupService::GetMetricSuffix() const {
  return ".Consumer";
}

bool RealTimeUrlLookupService::ShouldIncludeCredentials() const {
  return true;
}

}  // namespace safe_browsing
