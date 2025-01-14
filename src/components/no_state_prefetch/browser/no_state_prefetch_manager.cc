// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/no_state_prefetch/browser/no_state_prefetch_manager.h"

#include <stddef.h>

#include <algorithm>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "base/bind.h"
#include "base/callback_helpers.h"
#include "base/check_op.h"
#include "base/command_line.h"
#include "base/containers/contains.h"
#include "base/location.h"
#include "base/macros.h"
#include "base/memory/ptr_util.h"
#include "base/metrics/field_trial.h"
#include "base/metrics/histogram_macros.h"
#include "base/notreached.h"
#include "base/single_thread_task_runner.h"
#include "base/stl_util.h"
#include "base/strings/string_util.h"
#include "base/system/sys_info.h"
#include "base/threading/thread_task_runner_handle.h"
#include "base/time/default_tick_clock.h"
#include "base/time/time.h"
#include "base/timer/elapsed_timer.h"
#include "base/values.h"
#include "components/content_settings/core/browser/cookie_settings.h"
#include "components/no_state_prefetch/browser/no_state_prefetch_contents.h"
#include "components/no_state_prefetch/browser/no_state_prefetch_field_trial.h"
#include "components/no_state_prefetch/browser/no_state_prefetch_handle.h"
#include "components/no_state_prefetch/browser/no_state_prefetch_manager_delegate.h"
#include "components/no_state_prefetch/browser/prerender_histograms.h"
#include "components/no_state_prefetch/browser/prerender_history.h"
#include "components/no_state_prefetch/browser/prerender_util.h"
#include "components/no_state_prefetch/common/prerender_final_status.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/render_view_host.h"
#include "content/public/browser/session_storage_namespace.h"
#include "content/public/browser/site_instance.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_delegate.h"
#include "content/public/common/content_switches.h"
#include "content/public/common/url_constants.h"
#include "net/http/http_cache.h"
#include "net/http/http_request_headers.h"
#include "ui/gfx/geometry/rect.h"

using content::BrowserThread;
using content::RenderViewHost;
using content::SessionStorageNamespace;
using content::WebContents;

namespace prerender {

namespace {

// Time interval at which periodic cleanups are performed.
constexpr base::TimeDelta kPeriodicCleanupInterval =
    base::TimeDelta::FromMilliseconds(1000);

// Time interval after which OnCloseWebContentsDeleter will schedule a
// WebContents for deletion.
constexpr base::TimeDelta kDeleteWithExtremePrejudice =
    base::TimeDelta::FromSeconds(3);

// Length of prerender history, for display in chrome://net-internals
constexpr int kHistoryLength = 100;

}  // namespace

class NoStatePrefetchManager::OnCloseWebContentsDeleter
    : public content::WebContentsDelegate,
      public base::SupportsWeakPtr<
          NoStatePrefetchManager::OnCloseWebContentsDeleter> {
 public:
  OnCloseWebContentsDeleter(NoStatePrefetchManager* manager,
                            std::unique_ptr<WebContents> tab)
      : manager_(manager), tab_(std::move(tab)) {
    tab_->SetDelegate(this);
    base::ThreadTaskRunnerHandle::Get()->PostDelayedTask(
        FROM_HERE,
        base::BindOnce(
            &OnCloseWebContentsDeleter::ScheduleWebContentsForDeletion,
            AsWeakPtr(), /*timeout=*/true),
        kDeleteWithExtremePrejudice);
  }

  void CloseContents(WebContents* source) override {
    DCHECK_EQ(tab_.get(), source);
    ScheduleWebContentsForDeletion(/*timeout=*/false);
  }

 private:
  void ScheduleWebContentsForDeletion(bool timeout) {
    UMA_HISTOGRAM_BOOLEAN("Prerender.TabContentsDeleterTimeout", timeout);
    tab_->SetDelegate(nullptr);
    manager_->ScheduleDeleteOldWebContents(std::move(tab_), this);
    // |this| is deleted at this point.
  }

  NoStatePrefetchManager* const manager_;
  std::unique_ptr<WebContents> tab_;

  DISALLOW_COPY_AND_ASSIGN(OnCloseWebContentsDeleter);
};

NoStatePrefetchManagerObserver::~NoStatePrefetchManagerObserver() = default;

struct NoStatePrefetchManager::NavigationRecord {
  NavigationRecord(const GURL& url, base::TimeTicks time, Origin origin)
      : url(url), time(time), origin(origin) {}

  GURL url;
  base::TimeTicks time;
  Origin origin;
  FinalStatus final_status = FINAL_STATUS_UNKNOWN;
};

NoStatePrefetchManager::NoStatePrefetchManager(
    content::BrowserContext* browser_context,
    std::unique_ptr<NoStatePrefetchManagerDelegate> delegate)
    : browser_context_(browser_context),
      delegate_(std::move(delegate)),
      no_state_prefetch_contents_factory_(
          NoStatePrefetchContents::CreateFactory()),
      prerender_history_(std::make_unique<PrerenderHistory>(kHistoryLength)),
      histograms_(std::make_unique<PrerenderHistograms>()),
      tick_clock_(base::DefaultTickClock::GetInstance()) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

  last_prefetch_start_time_ =
      GetCurrentTimeTicks() -
      base::TimeDelta::FromMilliseconds(kMinTimeBetweenPrefetchesMs);
}

NoStatePrefetchManager::~NoStatePrefetchManager() {
  // The earlier call to KeyedService::Shutdown() should have
  // emptied these vectors already.
  DCHECK(active_prefetches_.empty());
  DCHECK(to_delete_prefetches_.empty());

  for (auto* host : prerender_process_hosts_) {
    host->RemoveObserver(this);
  }
}

void NoStatePrefetchManager::Shutdown() {
  DestroyAllContents(FINAL_STATUS_PROFILE_DESTROYED);
  on_close_web_contents_deleters_.clear();
  browser_context_ = nullptr;

  DCHECK(active_prefetches_.empty());
}

std::unique_ptr<NoStatePrefetchHandle>
NoStatePrefetchManager::AddPrerenderFromLinkRelPrerender(
    int process_id,
    int route_id,
    const GURL& url,
    blink::mojom::PrerenderRelType rel_type,
    const content::Referrer& referrer,
    const url::Origin& initiator_origin,
    const gfx::Size& size) {
  Origin origin = ORIGIN_LINK_REL_PRERENDER_CROSSDOMAIN;
  switch (rel_type) {
    case blink::mojom::PrerenderRelType::kPrerender:
      origin = ORIGIN_LINK_REL_PRERENDER_CROSSDOMAIN;
      break;
    case blink::mojom::PrerenderRelType::kNext:
      origin = ORIGIN_LINK_REL_NEXT;
      break;
  }

  SessionStorageNamespace* session_storage_namespace = nullptr;
  // Unit tests pass in a process_id == -1.
  if (process_id != -1) {
    RenderViewHost* source_render_view_host =
        RenderViewHost::FromID(process_id, route_id);
    if (!source_render_view_host)
      return nullptr;
    WebContents* source_web_contents =
        WebContents::FromRenderViewHost(source_render_view_host);
    if (!source_web_contents)
      return nullptr;
    if (origin == ORIGIN_LINK_REL_PRERENDER_CROSSDOMAIN &&
        source_web_contents->GetURL().host_piece() == url.host_piece()) {
      origin = ORIGIN_LINK_REL_PRERENDER_SAMEDOMAIN;
    }
    // TODO(ajwong): This does not correctly handle storage for isolated apps.
    session_storage_namespace = source_web_contents->GetController()
                                    .GetDefaultSessionStorageNamespace();
  }
  return AddPrerenderWithPreconnectFallback(origin, url, referrer,
                                            initiator_origin, gfx::Rect(size),
                                            session_storage_namespace);
}

std::unique_ptr<NoStatePrefetchHandle>
NoStatePrefetchManager::AddPrerenderFromOmnibox(
    const GURL& url,
    SessionStorageNamespace* session_storage_namespace,
    const gfx::Size& size) {
  return AddPrerenderWithPreconnectFallback(
      ORIGIN_OMNIBOX, url, content::Referrer(), base::nullopt, gfx::Rect(size),
      session_storage_namespace);
}

std::unique_ptr<NoStatePrefetchHandle>
NoStatePrefetchManager::AddPrerenderFromNavigationPredictor(
    const GURL& url,
    SessionStorageNamespace* session_storage_namespace,
    const gfx::Size& size) {
  return AddPrerenderWithPreconnectFallback(
      ORIGIN_NAVIGATION_PREDICTOR, url, content::Referrer(), base::nullopt,
      gfx::Rect(size), session_storage_namespace);
}

std::unique_ptr<NoStatePrefetchHandle>
NoStatePrefetchManager::AddIsolatedPrerender(
    const GURL& url,
    SessionStorageNamespace* session_storage_namespace,
    const gfx::Size& size) {
  // The preconnect fallback won't happen.
  return AddPrerenderWithPreconnectFallback(
      ORIGIN_ISOLATED_PRERENDER, url, content::Referrer(), base::nullopt,
      gfx::Rect(size), session_storage_namespace);
}

std::unique_ptr<NoStatePrefetchHandle>
NoStatePrefetchManager::AddPrerenderFromExternalRequest(
    const GURL& url,
    const content::Referrer& referrer,
    SessionStorageNamespace* session_storage_namespace,
    const gfx::Rect& bounds) {
  return AddPrerenderWithPreconnectFallback(ORIGIN_EXTERNAL_REQUEST, url,
                                            referrer, base::nullopt, bounds,
                                            session_storage_namespace);
}

std::unique_ptr<NoStatePrefetchHandle>
NoStatePrefetchManager::AddForcedPrerenderFromExternalRequest(
    const GURL& url,
    const content::Referrer& referrer,
    SessionStorageNamespace* session_storage_namespace,
    const gfx::Rect& bounds) {
  return AddPrerenderWithPreconnectFallback(
      ORIGIN_EXTERNAL_REQUEST_FORCED_PRERENDER, url, referrer, base::nullopt,
      bounds, session_storage_namespace);
}

void NoStatePrefetchManager::CancelAllPrerenders() {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  while (!active_prefetches_.empty()) {
    NoStatePrefetchContents* no_state_prefetch_contents =
        active_prefetches_.front()->contents();
    no_state_prefetch_contents->Destroy(FINAL_STATUS_CANCELLED);
  }
}

void NoStatePrefetchManager::MoveEntryToPendingDelete(
    NoStatePrefetchContents* entry,
    FinalStatus final_status) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  DCHECK(entry);

  auto it = FindIteratorForNoStatePrefetchContents(entry);
  DCHECK(it != active_prefetches_.end());
  to_delete_prefetches_.push_back(std::move(*it));
  active_prefetches_.erase(it);
  // Destroy the old WebContents relatively promptly to reduce resource usage.
  PostCleanupTask();
}

bool NoStatePrefetchManager::IsWebContentsPrerendering(
    const WebContents* web_contents) const {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  return GetNoStatePrefetchContents(web_contents);
}

NoStatePrefetchContents* NoStatePrefetchManager::GetNoStatePrefetchContents(
    const content::WebContents* web_contents) const {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  for (const auto& prefetch : active_prefetches_) {
    WebContents* prefetch_web_contents =
        prefetch->contents()->no_state_prefetch_contents();
    if (prefetch_web_contents == web_contents) {
      return prefetch->contents();
    }
  }

  // Also check the pending-deletion list. If the prefetch is in pending delete,
  // anyone with a handle on the WebContents needs to know.
  for (const auto& prefetch : to_delete_prefetches_) {
    WebContents* prefetch_web_contents =
        prefetch->contents()->no_state_prefetch_contents();
    if (prefetch_web_contents == web_contents) {
      return prefetch->contents();
    }
  }
  return nullptr;
}

NoStatePrefetchContents*
NoStatePrefetchManager::GetNoStatePrefetchContentsForRoute(int child_id,
                                                           int route_id) const {
  WebContents* web_contents = nullptr;
  RenderViewHost* render_view_host = RenderViewHost::FromID(child_id, route_id);
  web_contents = WebContents::FromRenderViewHost(render_view_host);
  return web_contents ? GetNoStatePrefetchContents(web_contents) : nullptr;
}

std::vector<WebContents*>
NoStatePrefetchManager::GetAllNoStatePrefetchingContentsForTesting() const {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  std::vector<WebContents*> result;

  for (const auto& prefetch : active_prefetches_) {
    WebContents* contents = prefetch->contents()->no_state_prefetch_contents();
    if (contents)
      result.push_back(contents);
  }

  return result;
}

bool NoStatePrefetchManager::HasRecentlyBeenNavigatedTo(Origin origin,
                                                        const GURL& url) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

  CleanUpOldNavigations(&navigations_, base::TimeDelta::FromMilliseconds(
                                           kNavigationRecordWindowMs));
  for (auto it = navigations_.rbegin(); it != navigations_.rend(); ++it) {
    if (it->url == url)
      return true;
  }

  return false;
}

std::unique_ptr<base::DictionaryValue> NoStatePrefetchManager::CopyAsValue()
    const {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

  auto dict_value = std::make_unique<base::DictionaryValue>();
  dict_value->Set("history", prerender_history_->CopyEntriesAsValue());
  dict_value->Set("active", GetActivePrerendersAsValue());
  dict_value->SetBoolean("enabled",
                         delegate_->IsNetworkPredictionPreferenceEnabled());
  dict_value->SetString("disabled_note",
                        delegate_->GetReasonForDisablingPrediction());
  // If prerender is disabled via a flag this method is not even called.
  std::string enabled_note;
  dict_value->SetString("enabled_note", enabled_note);
  return dict_value;
}

void NoStatePrefetchManager::ClearData(int clear_flags) {
  DCHECK_GE(clear_flags, 0);
  DCHECK_LT(clear_flags, CLEAR_MAX);
  if (clear_flags & CLEAR_PRERENDER_CONTENTS)
    DestroyAllContents(FINAL_STATUS_CACHE_OR_HISTORY_CLEARED);
  // This has to be second, since destroying prerenders can add to the history.
  if (clear_flags & CLEAR_PRERENDER_HISTORY)
    prerender_history_->Clear();
}

void NoStatePrefetchManager::RecordFinalStatus(Origin origin,
                                               FinalStatus final_status) const {
  histograms_->RecordFinalStatus(origin, final_status);
}

void NoStatePrefetchManager::RecordNavigation(const GURL& url) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

  navigations_.emplace_back(url, GetCurrentTimeTicks(), ORIGIN_NONE);
  CleanUpOldNavigations(&navigations_, base::TimeDelta::FromMilliseconds(
                                           kNavigationRecordWindowMs));
}

struct NoStatePrefetchManager::NoStatePrefetchData::OrderByExpiryTime {
  bool operator()(const std::unique_ptr<NoStatePrefetchData>& a,
                  const std::unique_ptr<NoStatePrefetchData>& b) const {
    return a->expiry_time() < b->expiry_time();
  }
};

NoStatePrefetchManager::NoStatePrefetchData::NoStatePrefetchData(
    NoStatePrefetchManager* manager,
    std::unique_ptr<NoStatePrefetchContents> contents,
    base::TimeTicks expiry_time)
    : manager_(manager),
      contents_(std::move(contents)),
      expiry_time_(expiry_time) {
  DCHECK(contents_);
}

NoStatePrefetchManager::NoStatePrefetchData::~NoStatePrefetchData() = default;

void NoStatePrefetchManager::NoStatePrefetchData::OnHandleCreated(
    NoStatePrefetchHandle* handle) {
  DCHECK(contents_);
  ++handle_count_;
  contents_->AddObserver(handle);
}

void NoStatePrefetchManager::NoStatePrefetchData::OnHandleNavigatedAway(
    NoStatePrefetchHandle* handle) {
  DCHECK_LT(0, handle_count_);
  DCHECK(contents_);
  if (abandon_time_.is_null())
    abandon_time_ = base::TimeTicks::Now();
  // We intentionally don't decrement the handle count here, so that the
  // prefetch won't be canceled until it times out.
  manager_->SourceNavigatedAway(this);
}

void NoStatePrefetchManager::NoStatePrefetchData::OnHandleCanceled(
    NoStatePrefetchHandle* handle) {
  DCHECK_LT(0, handle_count_);
  DCHECK(contents_);

  if (--handle_count_ == 0) {
    // This will eventually remove this object from |active_prefetches_|.
    contents_->Destroy(FINAL_STATUS_CANCELLED);
  }
}

std::unique_ptr<NoStatePrefetchContents>
NoStatePrefetchManager::NoStatePrefetchData::ReleaseContents() {
  return std::move(contents_);
}

void NoStatePrefetchManager::SourceNavigatedAway(
    NoStatePrefetchData* prefetch_data) {
  // The expiry time of our prefetch data will likely change because of
  // this navigation. This requires a re-sort of |active_prefetches_|.
  for (auto it = active_prefetches_.begin(); it != active_prefetches_.end();
       ++it) {
    NoStatePrefetchData* data = it->get();
    if (data == prefetch_data) {
      data->set_expiry_time(std::min(data->expiry_time(),
                                     GetExpiryTimeForNavigatedAwayPrerender()));
      SortActivePrefetches();
      return;
    }
  }
}

bool NoStatePrefetchManager::IsLowEndDevice() const {
  return base::SysInfo::IsLowEndDevice();
}

bool NoStatePrefetchManager::IsPredictionEnabled(Origin origin) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);

  // <link rel=prerender> and <link rel=next> origins ignore the network state
  // and the privacy
  // settings. Web developers should be able prefetch with all possible privacy
  // settings. This would avoid web devs coming up with creative ways to
  // prefetch in cases they are not allowed to do so.
  if (origin == ORIGIN_LINK_REL_PRERENDER_SAMEDOMAIN ||
      origin == ORIGIN_LINK_REL_PRERENDER_CROSSDOMAIN ||
      origin == ORIGIN_LINK_REL_NEXT) {
    return true;
  }

  // TODO(crbug.com/1121970): Remove this check once we're no longer running the
  // experiment "PredictivePrefetchingAllowedOnAllConnectionTypes".
  if (delegate_->IsPredictionDisabledDueToNetwork(origin))
    return false;

  return delegate_->IsNetworkPredictionPreferenceEnabled();
}

void NoStatePrefetchManager::MaybePreconnect(Origin origin,
                                             const GURL& url_arg) const {
  delegate_->MaybePreconnect(url_arg);
}

std::unique_ptr<NoStatePrefetchHandle>
NoStatePrefetchManager::AddPrerenderWithPreconnectFallback(
    Origin origin,
    const GURL& url_arg,
    const content::Referrer& referrer,
    const base::Optional<url::Origin>& initiator_origin,
    const gfx::Rect& bounds,
    SessionStorageNamespace* session_storage_namespace) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

  base::CommandLine* command_line = base::CommandLine::ForCurrentProcess();
  if (command_line && command_line->HasSwitch(switches::kSingleProcess)) {
    SkipNoStatePrefetchContentsAndMaybePreconnect(url_arg, origin,
                                                  FINAL_STATUS_SINGLE_PROCESS);
    return nullptr;
  }

  // Disallow NSPing link-rel:next URLs.
  // See https://bugs.chromium.org/p/chromium/issues/detail?id=1158209.
  if (origin == ORIGIN_LINK_REL_NEXT) {
    SkipNoStatePrefetchContentsAndMaybePreconnect(
        url_arg, origin, FINAL_STATUS_LINK_REL_NEXT_NOT_ALLOWED);
    return nullptr;
  }

  // Disallow prerendering on low end devices.
  if (IsLowEndDevice()) {
    SkipNoStatePrefetchContentsAndMaybePreconnect(url_arg, origin,
                                                  FINAL_STATUS_LOW_END_DEVICE);
    return nullptr;
  }

  if ((origin == ORIGIN_LINK_REL_PRERENDER_CROSSDOMAIN ||
       origin == ORIGIN_LINK_REL_PRERENDER_SAMEDOMAIN) &&
      IsGoogleOriginURL(referrer.url)) {
    origin = ORIGIN_GWS_PRERENDER;
  }

  GURL url = url_arg;

  if (delegate_->GetCookieSettings()->ShouldBlockThirdPartyCookies()) {
    SkipNoStatePrefetchContentsAndMaybePreconnect(
        url, origin, FINAL_STATUS_BLOCK_THIRD_PARTY_COOKIES);
    return nullptr;
  }

  if (!IsPredictionEnabled(origin)) {
    FinalStatus final_status =
        delegate_->IsPredictionDisabledDueToNetwork(origin)
            ? FINAL_STATUS_CELLULAR_NETWORK
            : FINAL_STATUS_PRERENDERING_DISABLED;
    SkipNoStatePrefetchContentsAndMaybePreconnect(url, origin, final_status);
    return nullptr;
  }

  if (NoStatePrefetchData* preexisting_prefetch_data =
          FindNoStatePrefetchData(url, session_storage_namespace)) {
    SkipNoStatePrefetchContentsAndMaybePreconnect(url, origin,
                                                  FINAL_STATUS_DUPLICATE);
    return base::WrapUnique(
        new NoStatePrefetchHandle(preexisting_prefetch_data));
  }

  base::TimeDelta prefetch_age;
  GetPrefetchInformation(url, &prefetch_age, nullptr /* final_status*/,
                         nullptr /* origin */);
  if (!prefetch_age.is_zero() &&
      prefetch_age <
          base::TimeDelta::FromMinutes(net::HttpCache::kPrefetchReuseMins)) {
    SkipNoStatePrefetchContentsAndMaybePreconnect(url, origin,
                                                  FINAL_STATUS_DUPLICATE);
    return nullptr;
  }

  // Do not prefetch if there are too many render processes, and we would have
  // to use an existing one.  We do not want prefetching to happen in a shared
  // process, so that we can always reliably lower the CPU priority for
  // prefetching.
  // In single-process mode, ShouldTryToUseExistingProcessHost() always returns
  // true, so that case needs to be explicitly checked for.
  // TODO(tburkard): Figure out how to cancel prefetching in the opposite case,
  // when a new tab is added to a process used for prefetching.
  // TODO(ppi): Check whether there are usually enough render processes
  // available on Android. If not, kill an existing renderers so that we can
  // create a new one.
  if (content::RenderProcessHost::ShouldTryToUseExistingProcessHost(
          browser_context_, url) &&
      !content::RenderProcessHost::run_renderer_in_process()) {
    SkipNoStatePrefetchContentsAndMaybePreconnect(
        url, origin, FINAL_STATUS_TOO_MANY_PROCESSES);
    return nullptr;
  }

  // Check if enough time has passed since the last prefetch.
  if (!DoesRateLimitAllowPrefetch(origin)) {
    // Cancel the prefetch. We could add it to the pending prefetch list but
    // this doesn't make sense as the next prefetch request will be triggered
    // by a navigation and is unlikely to be the same site.
    SkipNoStatePrefetchContentsAndMaybePreconnect(
        url, origin, FINAL_STATUS_RATE_LIMIT_EXCEEDED);
    return nullptr;
  }

  // Record the URL in the prefetch list, even when in full prerender mode, to
  // enable metrics comparisons.
  prefetches_.emplace_back(url, GetCurrentTimeTicks(), origin);

  // If this is GWS and we are in the holdback, skip the prefetch. Record the
  // status as holdback, so we can analyze via UKM.
  if (origin == ORIGIN_GWS_PRERENDER &&
      base::FeatureList::IsEnabled(kGWSPrefetchHoldback)) {
    // Set the holdback status on the prefetch entry.
    SetPrefetchFinalStatusForUrl(url, FINAL_STATUS_GWS_HOLDBACK);
    SkipNoStatePrefetchContentsAndMaybePreconnect(url, origin,
                                                  FINAL_STATUS_GWS_HOLDBACK);
    return nullptr;
  }

  // If this is Navigation predictor and we are in the holdback, skip the
  // prefetch. Record the status as holdback, so we can analyze via UKM.
  if (origin == ORIGIN_NAVIGATION_PREDICTOR &&
      base::FeatureList::IsEnabled(kNavigationPredictorPrefetchHoldback)) {
    // Set the holdback status on the prefetch entry.
    SetPrefetchFinalStatusForUrl(url,
                                 FINAL_STATUS_NAVIGATION_PREDICTOR_HOLDBACK);
    SkipNoStatePrefetchContentsAndMaybePreconnect(
        url, origin, FINAL_STATUS_NAVIGATION_PREDICTOR_HOLDBACK);
    return nullptr;
  }

  std::unique_ptr<NoStatePrefetchContents> no_state_prefetch_contents =
      CreateNoStatePrefetchContents(url, referrer, initiator_origin, origin);
  DCHECK(no_state_prefetch_contents);
  NoStatePrefetchContents* no_state_prefetch_contents_ptr =
      no_state_prefetch_contents.get();
  active_prefetches_.push_back(std::make_unique<NoStatePrefetchData>(
      this, std::move(no_state_prefetch_contents),
      GetExpiryTimeForNewPrerender(origin)));
  if (!no_state_prefetch_contents_ptr->Init()) {
    DCHECK(active_prefetches_.end() == FindIteratorForNoStatePrefetchContents(
                                           no_state_prefetch_contents_ptr));
    return nullptr;
  }

  DCHECK(!no_state_prefetch_contents_ptr->prerendering_has_started());

  std::unique_ptr<NoStatePrefetchHandle> no_state_prefetch_handle =
      base::WrapUnique(
          new NoStatePrefetchHandle(active_prefetches_.back().get()));
  SortActivePrefetches();

  last_prefetch_start_time_ = GetCurrentTimeTicks();

  gfx::Rect contents_bounds =
      bounds.IsEmpty() ? config_.default_tab_bounds : bounds;

  no_state_prefetch_contents_ptr->StartPrerendering(contents_bounds,
                                                    session_storage_namespace);

  DCHECK(no_state_prefetch_contents_ptr->prerendering_has_started());

  StartSchedulingPeriodicCleanups();
  return no_state_prefetch_handle;
}

void NoStatePrefetchManager::StartSchedulingPeriodicCleanups() {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  if (repeating_timer_.IsRunning())
    return;

  repeating_timer_.Start(FROM_HERE, kPeriodicCleanupInterval, this,
                         &NoStatePrefetchManager::PeriodicCleanup);
}

void NoStatePrefetchManager::StopSchedulingPeriodicCleanups() {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  repeating_timer_.Stop();
}

void NoStatePrefetchManager::PeriodicCleanup() {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

  base::ElapsedTimer resource_timer;

  // Grab a copy of the current NoStatePrefetchContents pointers, so that we
  // will not interfere with potential deletions of the list.
  std::vector<NoStatePrefetchContents*> prefetch_contents;
  prefetch_contents.reserve(active_prefetches_.size());
  for (auto& prefetch : active_prefetches_)
    prefetch_contents.push_back(prefetch->contents());

  // And now check for prerenders using too much memory.
  for (auto* contents : prefetch_contents)
    contents->DestroyWhenUsingTooManyResources();

  base::ElapsedTimer cleanup_timer;

  // Perform deferred cleanup work.
  DeleteOldWebContents();
  DeleteOldEntries();
  if (active_prefetches_.empty())
    StopSchedulingPeriodicCleanups();

  DeleteToDeletePrerenders();

  CleanUpOldNavigations(&prefetches_, base::TimeDelta::FromMinutes(30));
}

void NoStatePrefetchManager::PostCleanupTask() {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  base::ThreadTaskRunnerHandle::Get()->PostTask(
      FROM_HERE, base::BindOnce(&NoStatePrefetchManager::PeriodicCleanup,
                                weak_factory_.GetWeakPtr()));
}

base::TimeTicks NoStatePrefetchManager::GetExpiryTimeForNewPrerender(
    Origin origin) const {
  return GetCurrentTimeTicks() + config_.time_to_live;
}

base::TimeTicks NoStatePrefetchManager::GetExpiryTimeForNavigatedAwayPrerender()
    const {
  return GetCurrentTimeTicks() + config_.abandon_time_to_live;
}

void NoStatePrefetchManager::DeleteOldEntries() {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  while (!active_prefetches_.empty()) {
    auto& prefetch_data = active_prefetches_.front();
    DCHECK(prefetch_data);
    DCHECK(prefetch_data->contents());

    if (prefetch_data->expiry_time() > GetCurrentTimeTicks())
      return;
    prefetch_data->contents()->Destroy(FINAL_STATUS_TIMED_OUT);
  }
}

void NoStatePrefetchManager::DeleteToDeletePrerenders() {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  // Delete the items one by one (after removing from the vector) as deleting
  // the WebContents may trigger a call to GetNoStatePrefetchContents(), which
  // iterates over |to_delete_prefetches_|.
  while (!to_delete_prefetches_.empty()) {
    std::unique_ptr<NoStatePrefetchData> prefetch_data =
        std::move(to_delete_prefetches_.back());
    to_delete_prefetches_.pop_back();
  }
}

base::Time NoStatePrefetchManager::GetCurrentTime() const {
  return base::Time::Now();
}

base::TimeTicks NoStatePrefetchManager::GetCurrentTimeTicks() const {
  return tick_clock_->NowTicks();
}

void NoStatePrefetchManager::SetTickClockForTesting(
    const base::TickClock* tick_clock) {
  tick_clock_ = tick_clock;
}

void NoStatePrefetchManager::AddObserver(
    std::unique_ptr<NoStatePrefetchManagerObserver> observer) {
  observers_.push_back(std::move(observer));
}

std::unique_ptr<NoStatePrefetchContents>
NoStatePrefetchManager::CreateNoStatePrefetchContents(
    const GURL& url,
    const content::Referrer& referrer,
    const base::Optional<url::Origin>& initiator_origin,
    Origin origin) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  return base::WrapUnique(
      no_state_prefetch_contents_factory_->CreateNoStatePrefetchContents(
          delegate_->GetNoStatePrefetchContentsDelegate(), this,
          browser_context_, url, referrer, initiator_origin, origin));
}

void NoStatePrefetchManager::SortActivePrefetches() {
  std::sort(active_prefetches_.begin(), active_prefetches_.end(),
            NoStatePrefetchData::OrderByExpiryTime());
}

NoStatePrefetchManager::NoStatePrefetchData*
NoStatePrefetchManager::FindNoStatePrefetchData(
    const GURL& url,
    SessionStorageNamespace* session_storage_namespace) {
  for (const auto& prefetch : active_prefetches_) {
    NoStatePrefetchContents* contents = prefetch->contents();
    if (contents->Matches(url, session_storage_namespace))
      return prefetch.get();
  }
  return nullptr;
}

NoStatePrefetchManager::NoStatePrefetchDataVector::iterator
NoStatePrefetchManager::FindIteratorForNoStatePrefetchContents(
    NoStatePrefetchContents* no_state_prefetch_contents) {
  for (auto it = active_prefetches_.begin(); it != active_prefetches_.end();
       ++it) {
    if ((*it)->contents() == no_state_prefetch_contents)
      return it;
  }
  return active_prefetches_.end();
}

bool NoStatePrefetchManager::DoesRateLimitAllowPrefetch(Origin origin) const {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

  // Allow navigation predictor to manage its own rate limit.
  if (origin == ORIGIN_NAVIGATION_PREDICTOR)
    return true;
  base::TimeDelta elapsed_time =
      GetCurrentTimeTicks() - last_prefetch_start_time_;
  if (!config_.rate_limit_enabled)
    return true;
  return elapsed_time >=
         base::TimeDelta::FromMilliseconds(kMinTimeBetweenPrefetchesMs);
}

void NoStatePrefetchManager::DeleteOldWebContents() {
  old_web_contents_list_.clear();
}

bool NoStatePrefetchManager::GetPrefetchInformation(
    const GURL& url,
    base::TimeDelta* prefetch_age,
    FinalStatus* final_status,
    Origin* origin) {
  CleanUpOldNavigations(&prefetches_, base::TimeDelta::FromMinutes(30));

  if (prefetch_age)
    *prefetch_age = base::TimeDelta();
  if (final_status)
    *final_status = FINAL_STATUS_MAX;
  if (origin)
    *origin = ORIGIN_NONE;

  for (auto it = prefetches_.crbegin(); it != prefetches_.crend(); ++it) {
    if (it->url == url) {
      if (prefetch_age)
        *prefetch_age = GetCurrentTimeTicks() - it->time;
      if (final_status)
        *final_status = it->final_status;
      if (origin)
        *origin = it->origin;
      return true;
    }
  }
  return false;
}

void NoStatePrefetchManager::SetPrefetchFinalStatusForUrl(
    const GURL& url,
    FinalStatus final_status) {
  for (auto it = prefetches_.rbegin(); it != prefetches_.rend(); ++it) {
    if (it->url == url) {
      it->final_status = final_status;
      break;
    }
  }
}

bool NoStatePrefetchManager::HasRecentlyPrefetchedUrlForTesting(
    const GURL& url) {
  return std::any_of(prefetches_.cbegin(), prefetches_.cend(),
                     [url](const NavigationRecord& r) {
                       return r.url == url &&
                              r.final_status ==
                                  FINAL_STATUS_NOSTATE_PREFETCH_FINISHED;
                     });
}

void NoStatePrefetchManager::OnPrefetchUsed(const GURL& url) {
  // Loading a prefetched URL resets the revalidation bypass. Remove all
  // matching urls from the prefetch list for more accurate metrics.
  base::EraseIf(prefetches_,
                [url](const NavigationRecord& r) { return r.url == url; });
}

void NoStatePrefetchManager::CleanUpOldNavigations(
    std::vector<NavigationRecord>* navigations,
    base::TimeDelta max_age) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

  // Cutoff. Navigations before this cutoff can be discarded.
  base::TimeTicks cutoff = GetCurrentTimeTicks() - max_age;
  auto it = navigations->begin();
  for (; it != navigations->end(); ++it) {
    if (it->time > cutoff)
      break;
  }
  navigations->erase(navigations->begin(), it);
}

void NoStatePrefetchManager::ScheduleDeleteOldWebContents(
    std::unique_ptr<WebContents> tab,
    OnCloseWebContentsDeleter* deleter) {
  old_web_contents_list_.push_back(std::move(tab));
  PostCleanupTask();

  if (!deleter)
    return;

  for (auto it = on_close_web_contents_deleters_.begin();
       it != on_close_web_contents_deleters_.end(); ++it) {
    if (it->get() == deleter) {
      on_close_web_contents_deleters_.erase(it);
      return;
    }
  }
  NOTREACHED();
}

void NoStatePrefetchManager::AddToHistory(NoStatePrefetchContents* contents) {
  PrerenderHistory::Entry entry(contents->prerender_url(),
                                contents->final_status(), contents->origin(),
                                base::Time::Now());
  prerender_history_->AddEntry(entry);
}

std::unique_ptr<base::ListValue>
NoStatePrefetchManager::GetActivePrerendersAsValue() const {
  auto list_value = std::make_unique<base::ListValue>();
  for (const auto& prefetch : active_prefetches_) {
    auto prefetch_value = prefetch->contents()->GetAsValue();
    if (prefetch_value)
      list_value->Append(std::move(prefetch_value));
  }
  return list_value;
}

void NoStatePrefetchManager::DestroyAllContents(FinalStatus final_status) {
  DeleteOldWebContents();
  while (!active_prefetches_.empty()) {
    NoStatePrefetchContents* contents = active_prefetches_.front()->contents();
    contents->Destroy(final_status);
  }
  DeleteToDeletePrerenders();
}

void NoStatePrefetchManager::SkipNoStatePrefetchContentsAndMaybePreconnect(
    const GURL& url,
    Origin origin,
    FinalStatus final_status) const {
  PrerenderHistory::Entry entry(url, final_status, origin, base::Time::Now());
  prerender_history_->AddEntry(entry);
  histograms_->RecordFinalStatus(origin, final_status);

  if (origin == ORIGIN_ISOLATED_PRERENDER) {
    // Prefetch Proxy should not preconnect since that can't be done in a fully
    // isolated way.
    return;
  }

  if (origin == ORIGIN_LINK_REL_NEXT)
    return;

  if (final_status == FINAL_STATUS_LOW_END_DEVICE ||
      final_status == FINAL_STATUS_CELLULAR_NETWORK ||
      final_status == FINAL_STATUS_DUPLICATE ||
      final_status == FINAL_STATUS_TOO_MANY_PROCESSES) {
    MaybePreconnect(origin, url);
  }

  static_assert(
      FINAL_STATUS_MAX == FINAL_STATUS_LINK_REL_NEXT_NOT_ALLOWED + 1,
      "Consider whether a failed prefetch should fallback to preconnect");
}

void NoStatePrefetchManager::RecordNetworkBytesConsumed(
    Origin origin,
    int64_t prerender_bytes) {
  int64_t recent_browser_context_bytes =
      browser_context_network_bytes_ -
      last_recorded_browser_context_network_bytes_;
  last_recorded_browser_context_network_bytes_ = browser_context_network_bytes_;
  DCHECK_GE(recent_browser_context_bytes, 0);
  histograms_->RecordNetworkBytesConsumed(origin, prerender_bytes,
                                          recent_browser_context_bytes);
}

void NoStatePrefetchManager::AddPrerenderProcessHost(
    content::RenderProcessHost* process_host) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  bool inserted = prerender_process_hosts_.insert(process_host).second;
  DCHECK(inserted);
  process_host->AddObserver(this);
}

bool NoStatePrefetchManager::MayReuseProcessHost(
    content::RenderProcessHost* process_host) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  // Isolate prefetch processes to make the resource monitoring check more
  // accurate.
  return !base::Contains(prerender_process_hosts_, process_host);
}

void NoStatePrefetchManager::RenderProcessHostDestroyed(
    content::RenderProcessHost* host) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  size_t erased = prerender_process_hosts_.erase(host);
  host->RemoveObserver(this);
  DCHECK_EQ(1u, erased);
}

base::WeakPtr<NoStatePrefetchManager> NoStatePrefetchManager::AsWeakPtr() {
  return weak_factory_.GetWeakPtr();
}

void NoStatePrefetchManager::ClearPrefetchInformationForTesting() {
  prefetches_.clear();
}

std::unique_ptr<NoStatePrefetchHandle>
NoStatePrefetchManager::AddPrerenderWithPreconnectFallbackForTesting(
    Origin origin,
    const GURL& url,
    const base::Optional<url::Origin>& initiator_origin) {
  return AddPrerenderWithPreconnectFallback(
      origin, url, content::Referrer(), initiator_origin, gfx::Rect(), nullptr);
}

void NoStatePrefetchManager::SetNoStatePrefetchContentsFactoryForTest(
    NoStatePrefetchContents::Factory* no_state_prefetch_contents_factory) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  no_state_prefetch_contents_factory_.reset(no_state_prefetch_contents_factory);
}

}  // namespace prerender
