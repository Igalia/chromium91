// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "extensions/browser/api/declarative_net_request/file_backed_ruleset_source.h"

#include <memory>
#include <set>
#include <utility>

#include "base/bind.h"
#include "base/callback.h"
#include "base/callback_helpers.h"
#include "base/check_op.h"
#include "base/files/file_util.h"
#include "base/json/json_reader.h"
#include "base/json/json_string_value_serializer.h"
#include "base/memory/ptr_util.h"
#include "base/metrics/histogram_macros.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "base/timer/elapsed_timer.h"
#include "base/values.h"
#include "content/public/browser/browser_context.h"
#include "extensions/browser/api/declarative_net_request/constants.h"
#include "extensions/browser/api/declarative_net_request/parse_info.h"
#include "extensions/browser/api/declarative_net_request/utils.h"
#include "extensions/common/api/declarative_net_request.h"
#include "extensions/common/api/declarative_net_request/dnr_manifest_data.h"
#include "extensions/common/error_utils.h"
#include "extensions/common/file_util.h"
#include "extensions/common/install_warning.h"
#include "services/data_decoder/public/cpp/data_decoder.h"
#include "tools/json_schema_compiler/util.h"

namespace extensions {
namespace declarative_net_request {

namespace {

namespace dnr_api = extensions::api::declarative_net_request;
using Status = ReadJSONRulesResult::Status;

constexpr const char kFileDoesNotExistError[] = "File does not exist.";
constexpr const char kFileReadError[] = "File read error.";

constexpr const char kDynamicRulesetDirectory[] = "DNR Extension Rules";
constexpr const char kDynamicRulesJSONFilename[] = "rules.json";
constexpr const char kDynamicIndexedRulesFilename[] = "rules.fbs";

// Helper to retrieve the filename for the given |file_path|.
std::string GetFilename(const base::FilePath& file_path) {
  return file_path.BaseName().AsUTF8Unsafe();
}

std::string GetErrorWithFilename(const base::FilePath& json_path,
                                 const std::string& error) {
  return base::StrCat({GetFilename(json_path), ": ", error});
}

InstallWarning CreateInstallWarning(const base::FilePath& json_path,
                                    const std::string& message) {
  std::string message_with_filename = GetErrorWithFilename(json_path, message);
  return InstallWarning(message_with_filename,
                        dnr_api::ManifestKeys::kDeclarativeNetRequest,
                        dnr_api::DNRInfo::kRuleResources);
}

// Adds install warnings for rules which exceed the per-rule regex memory limit.
void AddRegexLimitExceededWarnings(
    const base::FilePath& json_path,
    std::vector<InstallWarning>* warnings,
    const std::vector<int>& regex_limit_exceeded_rule_ids) {
  DCHECK(warnings);

  std::vector<std::string> rule_ids;
  rule_ids.reserve(regex_limit_exceeded_rule_ids.size());
  for (int rule_id : regex_limit_exceeded_rule_ids)
    rule_ids.push_back(base::NumberToString(rule_id));

  constexpr size_t kMaxRegexLimitExceededWarnings = 10;
  if (rule_ids.size() <= kMaxRegexLimitExceededWarnings) {
    for (const std::string& rule_id : rule_ids) {
      warnings->push_back(CreateInstallWarning(
          json_path, ErrorUtils::FormatErrorMessage(kErrorRegexTooLarge,
                                                    rule_id, kRegexFilterKey)));
    }

    return;
  }

  warnings->push_back(CreateInstallWarning(
      json_path,
      ErrorUtils::FormatErrorMessage(
          kErrorRegexesTooLarge,
          base::JoinString(rule_ids, ", " /* separator */), kRegexFilterKey)));
}

ReadJSONRulesResult ParseRulesFromJSON(const RulesetID& ruleset_id,
                                       const base::FilePath& json_path,
                                       const base::Value& rules,
                                       size_t rule_limit,
                                       bool is_dynamic_ruleset) {
  ReadJSONRulesResult result;

  if (!rules.is_list()) {
    return ReadJSONRulesResult::CreateErrorResult(Status::kJSONIsNotList,
                                                  kErrorListNotPassed);
  }

  // Limit the maximum number of rule unparsed warnings to 5.
  const size_t kMaxUnparsedRulesWarnings = 5;
  bool unparsed_warnings_limit_exeeded = false;
  size_t unparsed_warning_count = 0;

  int regex_rule_count = 0;
  bool regex_rule_count_exceeded = false;

  // We don't use json_schema_compiler::util::PopulateArrayFromList since it
  // fails if a single Value can't be deserialized. However we want to ignore
  // values which can't be parsed to maintain backwards compatibility.
  const auto& rules_list = rules.GetList();

  // Ignore any rulesets which exceed the static rule count limit (This is
  // defined as dnr_api::GUARANTEED_MINIMUM_STATIC_RULES + the global rule count
  // limit). We do this because such a ruleset can never be enabled in its
  // entirety.
  if (rules_list.size() > rule_limit && !is_dynamic_ruleset) {
    result.status = ReadJSONRulesResult::Status::kRuleCountLimitExceeded;
    result.error = ErrorUtils::FormatErrorMessage(
        kIndexingRuleLimitExceeded, std::to_string(ruleset_id.value()));

    return result;
  }

  for (size_t i = 0; i < rules_list.size(); i++) {
    dnr_api::Rule parsed_rule;
    std::u16string parse_error;

    if (dnr_api::Rule::Populate(rules_list[i], &parsed_rule, &parse_error)) {
      DCHECK(parse_error.empty());
      if (result.rules.size() == rule_limit) {
        result.rule_parse_warnings.push_back(
            CreateInstallWarning(json_path, kRuleCountExceeded));
        break;
      }

      const bool is_regex_rule = !!parsed_rule.condition.regex_filter;
      if (is_regex_rule && ++regex_rule_count > GetRegexRuleLimit()) {
        // Only add the install warning once.
        if (!regex_rule_count_exceeded) {
          regex_rule_count_exceeded = true;
          result.rule_parse_warnings.push_back(
              CreateInstallWarning(json_path, kRegexRuleCountExceeded));
        }

        continue;
      }

      result.rules.push_back(std::move(parsed_rule));
      continue;
    }

    if (unparsed_warning_count == kMaxUnparsedRulesWarnings) {
      // Don't add the warning for the current rule.
      unparsed_warnings_limit_exeeded = true;
      continue;
    }

    ++unparsed_warning_count;
    std::string rule_location;

    // If possible use the rule ID in the install warning.
    if (auto* id_val =
            rules_list[i].FindKeyOfType(kIDKey, base::Value::Type::INTEGER)) {
      rule_location = base::StringPrintf("id %d", id_val->GetInt());
    } else {
      // Use one-based indices.
      rule_location = base::StringPrintf("index %zu", i + 1);
    }

    result.rule_parse_warnings.push_back(CreateInstallWarning(
        json_path,
        ErrorUtils::FormatErrorMessage(kRuleNotParsedWarning, rule_location,
                                       base::UTF16ToUTF8(parse_error))));
  }

  DCHECK_LE(result.rules.size(), rule_limit);

  if (unparsed_warnings_limit_exeeded) {
    result.rule_parse_warnings.push_back(CreateInstallWarning(
        json_path, ErrorUtils::FormatErrorMessage(
                       kTooManyParseFailuresWarning,
                       std::to_string(kMaxUnparsedRulesWarnings))));
  }

  return result;
}

IndexAndPersistJSONRulesetResult IndexAndPersistRuleset(
    const FileBackedRulesetSource& source,
    ReadJSONRulesResult read_result,
    const base::ElapsedTimer& timer) {
  // Rulesets which exceed the rule limit are ignored because they can never be
  // enabled without breaking the limit.
  if (read_result.status == Status::kRuleCountLimitExceeded) {
    std::vector<InstallWarning> warnings;
    warnings.push_back(
        CreateInstallWarning(source.json_path(), read_result.error));

    return IndexAndPersistJSONRulesetResult::CreateIgnoreResult(
        std::move(warnings));
  } else if (read_result.status != Status::kSuccess) {
    return IndexAndPersistJSONRulesetResult::CreateErrorResult(
        GetErrorWithFilename(source.json_path(), read_result.error));
  }

  DCHECK_EQ(Status::kSuccess, read_result.status);

  const ParseInfo info =
      source.IndexAndPersistRules(std::move(read_result.rules));

  if (info.has_error()) {
    std::string error = GetErrorWithFilename(source.json_path(), info.error());
    return IndexAndPersistJSONRulesetResult::CreateErrorResult(
        std::move(error));
  }

  // Don't cause a hard error if the regex failed compilation due to
  // exceeding the memory limit. This is because it's not a syntactical
  // error and the developers don't have an easy way to determine whether
  // the regex filter will exceed the memory limit or not. Also, the re2
  // implementation can change causing the memory consumption of a regex to
  // change as well.
  std::vector<InstallWarning> warnings =
      std::move(read_result.rule_parse_warnings);
  AddRegexLimitExceededWarnings(source.json_path(), &warnings,
                                info.regex_limit_exceeded_rules());

  return IndexAndPersistJSONRulesetResult::CreateSuccessResult(
      info.ruleset_checksum(), std::move(warnings), info.rules_count(),
      info.regex_rules_count(), timer.Elapsed());
}

void OnSafeJSONParse(
    const base::FilePath& json_path,
    const FileBackedRulesetSource& source,
    FileBackedRulesetSource::IndexAndPersistJSONRulesetCallback callback,
    data_decoder::DataDecoder::ValueOrError result) {
  if (!result.value) {
    std::move(callback).Run(IndexAndPersistJSONRulesetResult::CreateErrorResult(
        GetErrorWithFilename(json_path, *result.error)));
    return;
  }

  base::ElapsedTimer timer;
  ReadJSONRulesResult read_result = ParseRulesFromJSON(
      source.id(), json_path, *result.value, source.rule_count_limit(),
      source.is_dynamic_ruleset());

  std::move(callback).Run(
      IndexAndPersistRuleset(source, std::move(read_result), timer));
}

}  // namespace

// static
IndexAndPersistJSONRulesetResult
IndexAndPersistJSONRulesetResult::CreateSuccessResult(
    int ruleset_checksum,
    std::vector<InstallWarning> warnings,
    size_t rules_count,
    size_t regex_rules_count,
    base::TimeDelta index_and_persist_time) {
  IndexAndPersistJSONRulesetResult result;
  result.status = IndexAndPersistJSONRulesetResult::Status::kSuccess;
  result.ruleset_checksum = ruleset_checksum;
  result.warnings = std::move(warnings);
  result.rules_count = rules_count;
  result.regex_rules_count = regex_rules_count;
  result.index_and_persist_time = index_and_persist_time;
  return result;
}

// static
IndexAndPersistJSONRulesetResult
IndexAndPersistJSONRulesetResult::CreateIgnoreResult(
    std::vector<InstallWarning> warnings) {
  IndexAndPersistJSONRulesetResult result;
  result.status = IndexAndPersistJSONRulesetResult::Status::kIgnore;
  result.warnings = std::move(warnings);
  return result;
}

// static
IndexAndPersistJSONRulesetResult
IndexAndPersistJSONRulesetResult::CreateErrorResult(std::string error) {
  IndexAndPersistJSONRulesetResult result;
  result.status = IndexAndPersistJSONRulesetResult::Status::kError;
  result.error = std::move(error);
  return result;
}

IndexAndPersistJSONRulesetResult::~IndexAndPersistJSONRulesetResult() = default;
IndexAndPersistJSONRulesetResult::IndexAndPersistJSONRulesetResult(
    IndexAndPersistJSONRulesetResult&&) = default;
IndexAndPersistJSONRulesetResult& IndexAndPersistJSONRulesetResult::operator=(
    IndexAndPersistJSONRulesetResult&&) = default;
IndexAndPersistJSONRulesetResult::IndexAndPersistJSONRulesetResult() = default;

// static
ReadJSONRulesResult ReadJSONRulesResult::CreateErrorResult(Status status,
                                                           std::string error) {
  ReadJSONRulesResult result;
  result.status = status;
  result.error = std::move(error);
  return result;
}

ReadJSONRulesResult::ReadJSONRulesResult() = default;
ReadJSONRulesResult::~ReadJSONRulesResult() = default;
ReadJSONRulesResult::ReadJSONRulesResult(ReadJSONRulesResult&&) = default;
ReadJSONRulesResult& ReadJSONRulesResult::operator=(ReadJSONRulesResult&&) =
    default;

// static
std::vector<FileBackedRulesetSource> FileBackedRulesetSource::CreateStatic(
    const Extension& extension) {
  const std::vector<DNRManifestData::RulesetInfo>& rulesets =
      declarative_net_request::DNRManifestData::GetRulesets(extension);

  std::vector<FileBackedRulesetSource> sources;
  for (const auto& info : rulesets)
    sources.push_back(CreateStatic(extension, info));

  return sources;
}

FileBackedRulesetSource FileBackedRulesetSource::CreateStatic(
    const Extension& extension,
    const DNRManifestData::RulesetInfo& info) {
  return FileBackedRulesetSource(
      extension.path().Append(info.relative_path),
      extension.path().Append(
          file_util::GetIndexedRulesetRelativePath(info.id.value())),
      info.id, GetMaximumRulesPerRuleset(), extension.id(), info.enabled);
}

// static
FileBackedRulesetSource FileBackedRulesetSource::CreateDynamic(
    content::BrowserContext* context,
    const ExtensionId& extension_id) {
  base::FilePath dynamic_ruleset_directory =
      context->GetPath()
          .AppendASCII(kDynamicRulesetDirectory)
          .AppendASCII(extension_id);
  return FileBackedRulesetSource(
      dynamic_ruleset_directory.AppendASCII(kDynamicRulesJSONFilename),
      dynamic_ruleset_directory.AppendASCII(kDynamicIndexedRulesFilename),
      kDynamicRulesetID, GetDynamicAndSessionRuleLimit(), extension_id,
      true /* enabled_by_default */);
}

// static
std::unique_ptr<FileBackedRulesetSource>
FileBackedRulesetSource::CreateTemporarySource(RulesetID id,
                                               size_t rule_count_limit,
                                               ExtensionId extension_id) {
  base::FilePath temporary_file_indexed;
  base::FilePath temporary_file_json;
  if (!base::CreateTemporaryFile(&temporary_file_indexed) ||
      !base::CreateTemporaryFile(&temporary_file_json)) {
    return nullptr;
  }

  // Use WrapUnique since FileBackedRulesetSource constructor is private.
  return base::WrapUnique(new FileBackedRulesetSource(
      std::move(temporary_file_json), std::move(temporary_file_indexed), id,
      rule_count_limit, std::move(extension_id),
      true /* enabled_by_default */));
}

FileBackedRulesetSource::~FileBackedRulesetSource() = default;
FileBackedRulesetSource::FileBackedRulesetSource(FileBackedRulesetSource&&) =
    default;
FileBackedRulesetSource& FileBackedRulesetSource::operator=(
    FileBackedRulesetSource&&) = default;

FileBackedRulesetSource FileBackedRulesetSource::Clone() const {
  return FileBackedRulesetSource(json_path_, indexed_path_, id(),
                                 rule_count_limit(), extension_id(),
                                 enabled_by_default());
}

IndexAndPersistJSONRulesetResult
FileBackedRulesetSource::IndexAndPersistJSONRulesetUnsafe() const {
  base::ElapsedTimer timer;
  return IndexAndPersistRuleset(*this, ReadJSONRulesUnsafe(), timer);
}

void FileBackedRulesetSource::IndexAndPersistJSONRuleset(
    data_decoder::DataDecoder* decoder,
    IndexAndPersistJSONRulesetCallback callback) const {
  if (!base::PathExists(json_path_)) {
    std::move(callback).Run(IndexAndPersistJSONRulesetResult::CreateErrorResult(
        GetErrorWithFilename(json_path_, kFileDoesNotExistError)));
    return;
  }

  std::string json_contents;
  if (!base::ReadFileToString(json_path_, &json_contents)) {
    std::move(callback).Run(IndexAndPersistJSONRulesetResult::CreateErrorResult(
        GetErrorWithFilename(json_path_, kFileReadError)));
    return;
  }

  decoder->ParseJson(json_contents,
                     base::BindOnce(&OnSafeJSONParse, json_path_, Clone(),
                                    std::move(callback)));
}

ParseInfo FileBackedRulesetSource::IndexAndPersistRules(
    std::vector<dnr_api::Rule> rules) const {
  ParseInfo info = IndexRules(std::move(rules));
  if (info.has_error())
    return info;

  if (!PersistIndexedRuleset(indexed_path_, info.GetBuffer())) {
    return ParseInfo(ParseResult::ERROR_PERSISTING_RULESET,
                     nullptr /* rule_id */);
  }

  return info;
}

ReadJSONRulesResult FileBackedRulesetSource::ReadJSONRulesUnsafe() const {
  ReadJSONRulesResult result;

  if (!base::PathExists(json_path_)) {
    return ReadJSONRulesResult::CreateErrorResult(Status::kFileDoesNotExist,
                                                  kFileDoesNotExistError);
  }

  std::string json_contents;
  if (!base::ReadFileToString(json_path_, &json_contents)) {
    return ReadJSONRulesResult::CreateErrorResult(Status::kFileReadError,
                                                  kFileReadError);
  }

  base::JSONReader::ValueWithError value_with_error =
      base::JSONReader::ReadAndReturnValueWithError(
          json_contents, base::JSON_PARSE_RFC /* options */);
  if (!value_with_error.value) {
    return ReadJSONRulesResult::CreateErrorResult(
        Status::kJSONParseError, std::move(value_with_error.error_message));
  }

  return ParseRulesFromJSON(id(), json_path(), *value_with_error.value,
                            rule_count_limit(), is_dynamic_ruleset());
}

bool FileBackedRulesetSource::WriteRulesToJSON(
    const std::vector<dnr_api::Rule>& rules) const {
  DCHECK_LE(rules.size(), rule_count_limit());

  std::unique_ptr<base::Value> rules_value =
      json_schema_compiler::util::CreateValueFromArray(rules);
  DCHECK(rules_value);
  DCHECK(rules_value->is_list());

  if (!base::CreateDirectory(json_path_.DirName()))
    return false;

  std::string json_contents;
  JSONStringValueSerializer serializer(&json_contents);
  serializer.set_pretty_print(false);
  if (!serializer.Serialize(*rules_value))
    return false;

  int data_size = static_cast<int>(json_contents.size());
  return base::WriteFile(json_path_, json_contents.data(), data_size) ==
         data_size;
}

LoadRulesetResult FileBackedRulesetSource::CreateVerifiedMatcher(
    int expected_ruleset_checksum,
    std::unique_ptr<RulesetMatcher>* matcher) const {
  DCHECK(matcher);

  base::ElapsedTimer timer;

  if (!base::PathExists(indexed_path()))
    return LoadRulesetResult::kErrorInvalidPath;

  std::string ruleset_data;
  if (!base::ReadFileToString(indexed_path(), &ruleset_data))
    return LoadRulesetResult::kErrorCannotReadFile;

  if (!StripVersionHeaderAndParseVersion(&ruleset_data))
    return LoadRulesetResult::kErrorVersionMismatch;

  if (expected_ruleset_checksum !=
      GetChecksum(
          base::make_span(reinterpret_cast<const uint8_t*>(ruleset_data.data()),
                          ruleset_data.size()))) {
    return LoadRulesetResult::kErrorChecksumMismatch;
  }

  LoadRulesetResult result =
      RulesetSource::CreateVerifiedMatcher(std::move(ruleset_data), matcher);
  if (result == LoadRulesetResult::kSuccess) {
    UMA_HISTOGRAM_TIMES(
        "Extensions.DeclarativeNetRequest.CreateVerifiedMatcherTime",
        timer.Elapsed());
  }
  return result;
}

FileBackedRulesetSource::FileBackedRulesetSource(base::FilePath json_path,
                                                 base::FilePath indexed_path,
                                                 RulesetID id,
                                                 size_t rule_count_limit,
                                                 ExtensionId extension_id,
                                                 bool enabled_by_default)
    : RulesetSource(id,
                    rule_count_limit,
                    std::move(extension_id),
                    enabled_by_default),
      json_path_(std::move(json_path)),
      indexed_path_(std::move(indexed_path)) {}

}  // namespace declarative_net_request
}  // namespace extensions
