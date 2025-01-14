// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_AUTOFILL_CORE_BROWSER_UI_ACCESSORY_SHEET_DATA_H_
#define COMPONENTS_AUTOFILL_CORE_BROWSER_UI_ACCESSORY_SHEET_DATA_H_

#include <string>
#include <utility>
#include <vector>

#include "base/optional.h"
#include "base/types/strong_alias.h"
#include "components/autofill/core/browser/ui/accessory_sheet_enums.h"

namespace password_manager {
class IsPublicSuffixMatchTag;
}  // namespace password_manager

namespace autofill {

// Represents user data to be shown on the manual fallback UI (e.g. a Profile,
// or a Credit Card, or the credentials for a website).
class UserInfo {
 public:
  // Represents a selectable item, such as the username or a credit card
  // number.
  class Field {
   public:
    Field(std::u16string display_text,
          std::u16string a11y_description,
          bool is_obfuscated,
          bool selectable);
    Field(std::u16string display_text,
          std::u16string a11y_description,
          std::string id,
          bool is_obfuscated,
          bool selectable);
    Field(const Field& field);
    Field(Field&& field);

    ~Field();

    Field& operator=(const Field& field);
    Field& operator=(Field&& field);

    const std::u16string& display_text() const { return display_text_; }

    const std::u16string& a11y_description() const { return a11y_description_; }

    const std::string& id() const { return id_; }

    bool is_obfuscated() const { return is_obfuscated_; }

    bool selectable() const { return selectable_; }

    bool operator==(const UserInfo::Field& field) const;

    // Estimates dynamic memory usage.
    // See base/trace_event/memory_usage_estimator.h for more info.
    size_t EstimateMemoryUsage() const;

   private:
    // IMPORTANT(https://crbug.com/1169167): Add the size of newly added strings
    // to the memory estimation member!
    std::u16string display_text_;
    std::u16string a11y_description_;
    std::string id_;  // Optional, if needed to complete filling.
    bool is_obfuscated_;
    bool selectable_;
    size_t estimated_memory_use_by_strings_ = 0;
  };

  using IsPslMatch =
      base::StrongAlias<password_manager::IsPublicSuffixMatchTag, bool>;

  UserInfo();
  explicit UserInfo(std::string origin);
  UserInfo(std::string origin, IsPslMatch is_psl_match);
  UserInfo(const UserInfo& user_info);
  UserInfo(UserInfo&& field);

  ~UserInfo();

  UserInfo& operator=(const UserInfo& user_info);
  UserInfo& operator=(UserInfo&& user_info);

  void add_field(Field field) {
    estimated_dynamic_memory_use_ += field.EstimateMemoryUsage();
    fields_.push_back(std::move(field));
  }

  const std::vector<Field>& fields() const { return fields_; }
  const std::string& origin() const { return origin_; }
  IsPslMatch is_psl_match() const { return is_psl_match_; }

  bool operator==(const UserInfo& user_info) const;

  // Estimates dynamic memory usage.
  // See base/trace_event/memory_usage_estimator.h for more info.
  size_t EstimateMemoryUsage() const;

 private:
  // IMPORTANT(https://crbug.com/1169167): Add the size of newly added strings
  // to the memory estimation member!
  std::string origin_;
  IsPslMatch is_psl_match_{false};
  std::vector<Field> fields_;
  size_t estimated_dynamic_memory_use_ = 0;
};

std::ostream& operator<<(std::ostream& out, const UserInfo::Field& field);
std::ostream& operator<<(std::ostream& out, const UserInfo& user_info);

// Represents a command below the suggestions, such as "Manage password...".
class FooterCommand {
 public:
  FooterCommand(std::u16string display_text, autofill::AccessoryAction action);
  FooterCommand(const FooterCommand& footer_command);
  FooterCommand(FooterCommand&& footer_command);

  ~FooterCommand();

  FooterCommand& operator=(const FooterCommand& footer_command);
  FooterCommand& operator=(FooterCommand&& footer_command);

  const std::u16string& display_text() const { return display_text_; }

  autofill::AccessoryAction accessory_action() const {
    return accessory_action_;
  }

  bool operator==(const FooterCommand& fc) const;

  // Estimates dynamic memory usage.
  // See base/trace_event/memory_usage_estimator.h for more info.
  size_t EstimateMemoryUsage() const;

 private:
  // IMPORTANT(https://crbug.com/1169167): Add the size of newly added strings
  // to the memory estimation member!
  std::u16string display_text_;
  autofill::AccessoryAction accessory_action_;
  size_t estimated_memory_use_by_strings_ = 0;
};

std::ostream& operator<<(std::ostream& out, const FooterCommand& fc);

std::ostream& operator<<(std::ostream& out, const AccessoryTabType& type);

// Toggle to be displayed above the suggestions. One such toggle can be used,
// for example, to turn password saving on for the current origin.
class OptionToggle {
 public:
  OptionToggle(std::u16string display_text,
               bool enabled,
               AccessoryAction accessory_action);
  OptionToggle(const OptionToggle& option_toggle);
  OptionToggle(OptionToggle&& option_toggle);

  ~OptionToggle();

  OptionToggle& operator=(const OptionToggle& option_toggle);
  OptionToggle& operator=(OptionToggle&& option_toggle);

  const std::u16string& display_text() const { return display_text_; }

  bool is_enabled() const { return enabled_; }

  AccessoryAction accessory_action() const { return accessory_action_; }

  bool operator==(const OptionToggle& option_toggle) const;

  // Estimates dynamic memory usage.
  // See base/trace_event/memory_usage_estimator.h for more info.
  size_t EstimateMemoryUsage() const;

 private:
  // IMPORTANT(https://crbug.com/1169167): Add the size of newly added strings
  // to the memory estimation member!
  std::u16string display_text_;
  bool enabled_;
  autofill::AccessoryAction accessory_action_;
  size_t estimated_memory_use_by_strings_ = 0;
};

// Represents the contents of a bottom sheet tab below the keyboard accessory,
// which can correspond to passwords, credit cards, or profiles data.
class AccessorySheetData {
 public:
  class Builder;

  AccessorySheetData(AccessoryTabType sheet_type, std::u16string title);
  AccessorySheetData(AccessoryTabType sheet_type,
                     std::u16string title,
                     std::u16string warning);
  AccessorySheetData(const AccessorySheetData& data);
  AccessorySheetData(AccessorySheetData&& data);

  ~AccessorySheetData();

  AccessorySheetData& operator=(const AccessorySheetData& data);
  AccessorySheetData& operator=(AccessorySheetData&& data);

  const std::u16string& title() const { return title_; }
  AccessoryTabType get_sheet_type() const { return sheet_type_; }

  const std::u16string& warning() const { return warning_; }
  void set_warning(std::u16string warning) { warning_ = std::move(warning); }

  void set_option_toggle(OptionToggle toggle) {
    option_toggle_ = std::move(toggle);
  }
  const base::Optional<OptionToggle>& option_toggle() const {
    return option_toggle_;
  }

  void add_user_info(UserInfo user_info) {
    user_info_list_.emplace_back(std::move(user_info));
  }

  const std::vector<UserInfo>& user_info_list() const {
    return user_info_list_;
  }

  std::vector<UserInfo>& mutable_user_info_list() { return user_info_list_; }

  void add_footer_command(FooterCommand footer_command) {
    footer_commands_.emplace_back(std::move(footer_command));
  }

  const std::vector<FooterCommand>& footer_commands() const {
    return footer_commands_;
  }

  bool operator==(const AccessorySheetData& data) const;

  // Estimates dynamic memory usage.
  // See base/trace_event/memory_usage_estimator.h for more info.
  size_t EstimateMemoryUsage() const;

 private:
  AccessoryTabType sheet_type_;
  std::u16string title_;
  std::u16string warning_;
  base::Optional<OptionToggle> option_toggle_;
  std::vector<UserInfo> user_info_list_;
  std::vector<FooterCommand> footer_commands_;
};

std::ostream& operator<<(std::ostream& out, const AccessorySheetData& data);

// Helper class for AccessorySheetData objects creation.
//
// Example that creates a AccessorySheetData object with two UserInfo objects;
// the former has two fields, whereas the latter has three fields:
//   AccessorySheetData data = AccessorySheetData::Builder(title)
//       .AddUserInfo()
//           .AppendField(...)
//           .AppendField(...)
//       .AddUserInfo()
//           .AppendField(...)
//           .AppendField(...)
//           .AppendField(...)
//       .Build();
class AccessorySheetData::Builder {
 public:
  Builder(AccessoryTabType type, std::u16string title);
  ~Builder();

  // Adds a warning string to the accessory sheet.
  Builder&& SetWarning(std::u16string warning) &&;
  Builder& SetWarning(std::u16string warning) &;

  // Sets the option toggle in the accessory sheet.
  Builder&& SetOptionToggle(std::u16string display_text,
                            bool enabled,
                            autofill::AccessoryAction action) &&;
  Builder& SetOptionToggle(std::u16string display_text,
                           bool enabled,
                           autofill::AccessoryAction action) &;

  // Adds a new UserInfo object to |accessory_sheet_data_|.
  Builder&& AddUserInfo(
      std::string origin = std::string(),
      UserInfo::IsPslMatch is_psl_match = UserInfo::IsPslMatch(false)) &&;
  Builder& AddUserInfo(
      std::string origin = std::string(),
      UserInfo::IsPslMatch is_psl_match = UserInfo::IsPslMatch(false)) &;

  // Appends a selectable, non-obfuscated field to the last UserInfo object.
  Builder&& AppendSimpleField(std::u16string text) &&;
  Builder& AppendSimpleField(std::u16string text) &;

  // Appends a field to the last UserInfo object.
  Builder&& AppendField(std::u16string display_text,
                        std::u16string a11y_description,
                        bool is_obfuscated,
                        bool selectable) &&;
  Builder& AppendField(std::u16string display_text,
                       std::u16string a11y_description,
                       bool is_obfuscated,
                       bool selectable) &;

  Builder&& AppendField(std::u16string display_text,
                        std::u16string a11y_description,
                        std::string id,
                        bool is_obfuscated,
                        bool selectable) &&;
  Builder& AppendField(std::u16string display_text,
                       std::u16string a11y_description,
                       std::string id,
                       bool is_obfuscated,
                       bool selectable) &;

  // Appends a new footer command to |accessory_sheet_data_|.
  Builder&& AppendFooterCommand(std::u16string display_text,
                                autofill::AccessoryAction action) &&;
  Builder& AppendFooterCommand(std::u16string display_text,
                               autofill::AccessoryAction action) &;

  // This class returns the constructed AccessorySheetData object. Since this
  // would render the builder unusable, it's required to destroy the object
  // afterwards. So if you hold the class in a variable, invoke like this:
  //   AccessorySheetData::Builder b(title);
  //   std::move(b).Build();
  AccessorySheetData&& Build() &&;

 private:
  AccessorySheetData accessory_sheet_data_;
};

}  // namespace autofill

#endif  // COMPONENTS_AUTOFILL_CORE_BROWSER_UI_ACCESSORY_SHEET_DATA_H_
