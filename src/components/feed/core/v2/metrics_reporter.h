// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_FEED_CORE_V2_METRICS_REPORTER_H_
#define COMPONENTS_FEED_CORE_V2_METRICS_REPORTER_H_

#include <climits>
#include <map>

#include "base/memory/weak_ptr.h"
#include "base/optional.h"
#include "base/time/time.h"
#include "components/feed/core/v2/common_enums.h"
#include "components/feed/core/v2/enums.h"
#include "components/feed/core/v2/public/feed_api.h"
#include "components/feed/core/v2/public/web_feed_subscriptions.h"
#include "components/feed/core/v2/types.h"

class PrefService;
namespace feed {

// Reports UMA metrics for feed.
// Note this is inherited only for testing.
class MetricsReporter {
 public:
  // For 'index_in_stream' parameters, when the card index is unknown.
  // This is most likely to happen when the action originates from the bottom
  // sheet.
  static const int kUnknownCardIndex = INT_MAX;

  explicit MetricsReporter(PrefService* profile_prefs);
  virtual ~MetricsReporter();
  MetricsReporter(const MetricsReporter&) = delete;
  MetricsReporter& operator=(const MetricsReporter&) = delete;

  // User interactions. See |FeedApi| for definitions.

  virtual void ContentSliceViewed(const StreamType& stream_type,
                                  int index_in_stream);
  void FeedViewed(SurfaceId surface_id);
  void OpenAction(const StreamType& stream_type, int index_in_stream);
  void OpenVisitComplete(base::TimeDelta visit_time);
  void OpenInNewTabAction(const StreamType& stream_type, int index_in_stream);
  void PageLoaded();
  void OtherUserAction(const StreamType& stream_type,
                       FeedUserActionType action_type);

  // Indicates the user scrolled the feed by |distance_dp| and then stopped
  // scrolling.
  void StreamScrolled(const StreamType& stream_type, int distance_dp);
  void StreamScrollStart();

  // Called when the Feed surface is opened and closed.
  void SurfaceOpened(SurfaceId surface_id);
  void SurfaceClosed(SurfaceId surface_id);

  // Network metrics.

  static void NetworkRequestComplete(NetworkRequestType type,
                                     int http_status_code);

  // Stream events.

  virtual void OnLoadStream(LoadStreamStatus load_from_store_status,
                            LoadStreamStatus final_status,
                            bool loaded_new_content_from_network,
                            base::TimeDelta stored_content_age,
                            std::unique_ptr<LoadLatencyTimes> load_latencies);
  virtual void OnBackgroundRefresh(LoadStreamStatus final_status);
  virtual void OnLoadMoreBegin(SurfaceId surface_id);
  virtual void OnLoadMore(LoadStreamStatus final_status);
  virtual void OnClearAll(base::TimeDelta time_since_last_clear);
  // Called each time the surface receives new content.
  void SurfaceReceivedContent(SurfaceId surface_id);
  // Called when Chrome is entering the background.
  void OnEnterBackground();

  static void OnImageFetched(int net_error_or_http_status);

  // Actions upload.
  static void OnUploadActionsBatch(UploadActionsBatchStatus status);
  virtual void OnUploadActions(UploadActionsStatus status);

  static void ActivityLoggingEnabled(bool response_has_logging_enabled);
  static void NoticeCardFulfilled(bool response_has_notice_card);
  static void NoticeCardFulfilledObsolete(bool response_has_notice_card);

  // Web Feed events.
  void OnFollowAttempt(const WebFeedSubscriptions::FollowWebFeedResult& result);
  void OnUnfollowAttempt(
      const WebFeedSubscriptions::UnfollowWebFeedResult& status);
  void RefreshRecommendedWebFeedsAttempted(WebFeedRefreshStatus status,
                                           int recommended_web_feed_count);
  void RefreshSubscribedWebFeedsAttempted(WebFeedRefreshStatus status,
                                          int subscribed_web_feed_count);

 private:
  // State replicated for reporting per-stream-type metrics.
  struct StreamStats {
    bool engaged_simple_reported_ = false;
    bool engaged_reported_ = false;
    bool scrolled_reported_ = false;
  };
  base::WeakPtr<MetricsReporter> GetWeakPtr() {
    return weak_ptr_factory_.GetWeakPtr();
  }

  void ReportPersistentDataIfDayIsDone();
  void CardOpenBegin();
  void CardOpenTimeout(base::TimeTicks start_ticks);
  void ReportCardOpenEndIfNeeded(bool success);
  void RecordEngagement(const StreamType& stream_type,
                        int scroll_distance_dp,
                        bool interacted);
  void TrackTimeSpentInFeed(bool interacted_or_scrolled);
  void RecordInteraction(const StreamType& stream_type);
  void ReportOpenFeedIfNeeded(SurfaceId surface_id, bool success);
  void ReportGetMoreIfNeeded(SurfaceId surface_id, bool success);
  void FinalizeMetrics();
  void FinalizeVisit();
  StreamStats& ForStream(const StreamType& stream_type);

  PrefService* profile_prefs_;

  StreamStats for_you_stats_;
  StreamStats web_feed_stats_;

  // State below here is shared between all stream types.

  // Persistent data stored in prefs. Data is read in the constructor, and then
  // written back to prefs on backgrounding.
  PersistentMetricsData persistent_data_;

  base::TimeTicks visit_start_time_;

  // The time a surface was opened, for surfaces still waiting for content.
  std::map<SurfaceId, base::TimeTicks> surfaces_waiting_for_content_;
  // The time a surface requested more content, for surfaces still waiting for
  // more content.
  std::map<SurfaceId, base::TimeTicks> surfaces_waiting_for_more_content_;

  // Tracking ContentSuggestions.Feed.UserJourney.OpenCard.*:
  // We assume at most one card is opened at a time. The time the card was
  // tapped is stored here. Upon timeout, another open attempt, or
  // |ChromeStopping()|, the open is considered failed. Otherwise, if the
  // loading the page succeeds, the open is considered successful.
  base::Optional<base::TimeTicks> pending_open_;

  // For tracking time spent in the Feed.
  base::Optional<base::TimeTicks> time_in_feed_start_;
  // For TimeSpentOnFeed.
  base::TimeDelta tracked_visit_time_in_feed_;
  // Non-null only directly after a stream load.
  std::unique_ptr<LoadLatencyTimes> load_latencies_;
  bool load_latencies_recorded_ = false;

  base::WeakPtrFactory<MetricsReporter> weak_ptr_factory_{this};
};
}  // namespace feed

#endif  // COMPONENTS_FEED_CORE_V2_METRICS_REPORTER_H_
