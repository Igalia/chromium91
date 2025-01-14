// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/sync/engine/commit_processor.h"

#include <memory>
#include <utility>

#include "base/logging.h"
#include "base/metrics/histogram_macros.h"
#include "base/notreached.h"
#include "components/sync/engine/commit_contribution.h"
#include "components/sync/engine/commit_contributor.h"
#include "components/sync/protocol/sync.pb.h"

namespace syncer {

using TypeToIndexMap = std::map<ModelType, size_t>;

CommitProcessor::CommitProcessor(ModelTypeSet commit_types,
                                 CommitContributorMap* commit_contributor_map)
    : commit_types_(commit_types),
      commit_contributor_map_(commit_contributor_map),
      phase_(GatheringPhase::kPriority) {
  // NIGORI contributions must be collected in every commit cycle.
  DCHECK(commit_types_.Has(NIGORI));
  DCHECK(commit_contributor_map);
}

CommitProcessor::~CommitProcessor() {}

Commit::ContributionMap CommitProcessor::GatherCommitContributions(
    size_t max_entries) {
  DCHECK_GT(max_entries, 0u);
  if (phase_ == GatheringPhase::kDone) {
    return Commit::ContributionMap();
  }

  Commit::ContributionMap contributions;

  // NIGORI contributions are always gathered to make sure that no encrypted
  // data gets committed before the corresponding NIGORI commit, which can
  // otherwise leave to data loss if the commit fails partially.
  size_t num_entries =
      GatherCommitContributionsForType(NIGORI, max_entries, &contributions);
  if (num_entries > 0) {
    // Encryptable entities cannot get combined in the same commit with NIGORI.
    // NIGORI commits are rare so to keep it simple and to play it safe, the
    // processor does not combine any other entities with NIGORI.
    return contributions;
  }

  num_entries += GatherCommitContributionsForTypes(
      GetUserTypesForCurrentCommitPhase(), max_entries - num_entries,
      &contributions);
  DCHECK_LE(num_entries, max_entries);
  if (num_entries < max_entries) {
    // Move to the next phase because there are no further commit contributions
    // for this phase at this moment (as there's still capacity left). Even if
    // new contributions for this phase appear while this commit is in flight,
    // they will get ignored until the next nudge. This prevents infinite commit
    // cycles.
    phase_ = IncrementGatheringPhase(phase_);

    if (num_entries == 0) {
      // If there are no entries in this phase, return contributions from the
      // next phase immediately. Otherwise, the processor gathers contribution
      // from the next phase in the next commit.
      return GatherCommitContributions(max_entries);
    }
  }
  return contributions;
}

// static
CommitProcessor::GatheringPhase CommitProcessor::IncrementGatheringPhase(
    GatheringPhase phase) {
  switch (phase) {
    case GatheringPhase::kPriority:
      return GatheringPhase::kRegular;
    case GatheringPhase::kRegular:
      return GatheringPhase::kDone;
    case GatheringPhase::kDone:
      NOTREACHED();
      return GatheringPhase::kDone;
  }
}

ModelTypeSet CommitProcessor::GetUserTypesForCurrentCommitPhase() const {
  switch (phase_) {
    case GatheringPhase::kPriority:
      return Intersection(commit_types_, PriorityUserTypes());
    case GatheringPhase::kRegular:
      return Difference(commit_types_,
                        Union(PriorityUserTypes(), ModelTypeSet(NIGORI)));
    case GatheringPhase::kDone:
      NOTREACHED();
      return ModelTypeSet();
  }
}

size_t CommitProcessor::GatherCommitContributionsForType(
    ModelType type,
    size_t max_entries,
    Commit::ContributionMap* contributions) {
  if (max_entries == 0) {
    return 0;
  }
  auto cm_it = commit_contributor_map_->find(type);
  if (cm_it == commit_contributor_map_->end()) {
    DLOG(ERROR) << "Could not find requested type " << ModelTypeToString(type)
                << " in contributor map.";
    return 0;
  }

  std::unique_ptr<CommitContribution> contribution =
      cm_it->second->GetContribution(max_entries);
  if (!contribution) {
    return 0;
  }

  size_t num_entries = contribution->GetNumEntries();
  DCHECK_LE(num_entries, max_entries);
  contributions->emplace(type, std::move(contribution));

  return num_entries;
}

size_t CommitProcessor::GatherCommitContributionsForTypes(
    ModelTypeSet types,
    size_t max_entries,
    Commit::ContributionMap* contributions) {
  size_t num_entries = 0;
  for (ModelType type : types) {
    num_entries += GatherCommitContributionsForType(
        type, max_entries - num_entries, contributions);
    if (num_entries >= max_entries) {
      DCHECK_EQ(num_entries, max_entries)
          << "Number of commit entries exceeds maximum";
      break;
    }
  }
  return num_entries;
}

}  // namespace syncer
