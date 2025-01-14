// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/signin/internal/identity_manager/primary_account_mutator_impl.h"

#include <string>

#include "base/check.h"
#include "build/chromeos_buildflags.h"
#include "components/prefs/pref_service.h"
#include "components/signin/internal/identity_manager/account_tracker_service.h"
#include "components/signin/internal/identity_manager/primary_account_manager.h"
#include "components/signin/internal/identity_manager/profile_oauth2_token_service.h"
#include "components/signin/public/base/signin_buildflags.h"
#include "components/signin/public/base/signin_metrics.h"
#include "components/signin/public/base/signin_pref_names.h"
#include "google_apis/gaia/core_account_id.h"

namespace signin {

PrimaryAccountMutatorImpl::PrimaryAccountMutatorImpl(
    AccountTrackerService* account_tracker,
    ProfileOAuth2TokenService* token_service,
    PrimaryAccountManager* primary_account_manager,
    PrefService* pref_service,
    signin::AccountConsistencyMethod account_consistency)
    : account_tracker_(account_tracker),
      token_service_(token_service),
      primary_account_manager_(primary_account_manager),
      pref_service_(pref_service),
      account_consistency_(account_consistency) {
  DCHECK(account_tracker_);
  DCHECK(token_service_);
  DCHECK(primary_account_manager_);
  DCHECK(pref_service_);

#if BUILDFLAG(IS_CHROMEOS_ASH)
  // |account_consistency_| is not used on CHROMEOS_ASH, however it is preferred
  // to have it defined to avoid a lof of ifdefs in the header file.
  signin::AccountConsistencyMethod unused = account_consistency_;
  ALLOW_UNUSED_LOCAL(unused);
#endif
}

PrimaryAccountMutatorImpl::~PrimaryAccountMutatorImpl() {}

bool PrimaryAccountMutatorImpl::SetPrimaryAccount(
    const CoreAccountId& account_id) {
  AccountInfo account_info = account_tracker_->GetAccountInfo(account_id);

#if !BUILDFLAG(IS_CHROMEOS_ASH)
  if (!pref_service_->GetBoolean(prefs::kSigninAllowed))
    return false;

  if (primary_account_manager_->HasPrimaryAccount(ConsentLevel::kSync))
    return false;

  if (account_info.account_id != account_id || account_info.email.empty())
    return false;

  // TODO(crbug.com/889899): should check that the account email is allowed.
#endif

  primary_account_manager_->SetSyncPrimaryAccountInfo(account_info);
  return true;
}

void PrimaryAccountMutatorImpl::SetUnconsentedPrimaryAccount(
    const CoreAccountId& account_id) {
#if BUILDFLAG(IS_CHROMEOS_ASH)
  // On Chrome OS the UPA can only be set once and never removed or changed.
  DCHECK(!account_id.empty());
  DCHECK(!primary_account_manager_->HasPrimaryAccount(ConsentLevel::kSignin));
#endif
  AccountInfo account_info;
  if (!account_id.empty()) {
    account_info = account_tracker_->GetAccountInfo(account_id);
    DCHECK(!account_info.IsEmpty());
  }

  primary_account_manager_->SetUnconsentedPrimaryAccountInfo(account_info);
}

#if !BUILDFLAG(IS_CHROMEOS_ASH)
bool PrimaryAccountMutatorImpl::RevokeConsentShouldClearPrimaryAccount() const {
  switch (account_consistency_) {
    case AccountConsistencyMethod::kDice:
      // If DICE is enabled, then adding and removing accounts is handled from
      // the Google web services. This means that the user needs to be signed
      // in to the their Google account on the web in order to be able to sign
      // out of that accounts. As in most cases, the Google auth cookies are
      // are derived from the refresh token, which means that the user is signed
      // out of their Google account on the web when the primary account is in
      // an auth error. It is therefore important to clear all accounts when
      // the user revokes their sync consent for a primary account that is in
      // an auth error as otherwise the user will not be able to remove it from
      // Chrome.
      //
      // TODO(msarda): The logic in this function is platform specific and we
      // should consider moving it to |SigninManager|.
      return token_service_->RefreshTokenHasError(
          primary_account_manager_->GetPrimaryAccountId(ConsentLevel::kSync));
    case AccountConsistencyMethod::kDisabled:
    case AccountConsistencyMethod::kMirror:
      return true;
  }
}
#endif

void PrimaryAccountMutatorImpl::RevokeSyncConsent(
    signin_metrics::ProfileSignout source_metric,
    signin_metrics::SignoutDelete delete_metric) {
  DCHECK(primary_account_manager_->HasPrimaryAccount(ConsentLevel::kSync));
#if !BUILDFLAG(IS_CHROMEOS_ASH)
  if (RevokeConsentShouldClearPrimaryAccount()) {
    ClearPrimaryAccount(source_metric, delete_metric);
    return;
  }
#endif
  primary_account_manager_->RevokeSyncConsent(source_metric, delete_metric);
}

#if !BUILDFLAG(IS_CHROMEOS_ASH)
bool PrimaryAccountMutatorImpl::ClearPrimaryAccount(
    signin_metrics::ProfileSignout source_metric,
    signin_metrics::SignoutDelete delete_metric) {
  if (!primary_account_manager_->HasPrimaryAccount(ConsentLevel::kSignin))
    return false;

  primary_account_manager_->ClearPrimaryAccount(source_metric, delete_metric);
  return true;
}
#endif

}  // namespace signin
