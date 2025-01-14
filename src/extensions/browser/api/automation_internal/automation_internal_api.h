// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef EXTENSIONS_BROWSER_API_AUTOMATION_INTERNAL_AUTOMATION_INTERNAL_API_H_
#define EXTENSIONS_BROWSER_API_AUTOMATION_INTERNAL_AUTOMATION_INTERNAL_API_H_

#include <string>

#include "base/optional.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/browser/web_contents_user_data.h"
#include "extensions/browser/extension_function.h"

namespace ui {
struct AXActionData;
}  // namespace ui

namespace extensions {

struct AutomationInfo;

// Implementation of the chrome.automation API.
class AutomationInternalEnableTabFunction : public ExtensionFunction {
  DECLARE_EXTENSION_FUNCTION("automationInternal.enableTab",
                             AUTOMATIONINTERNAL_ENABLETAB)
 protected:
  ~AutomationInternalEnableTabFunction() override = default;

  ExtensionFunction::ResponseAction Run() override;
};

class AutomationInternalPerformActionFunction : public ExtensionFunction {
  DECLARE_EXTENSION_FUNCTION("automationInternal.performAction",
                             AUTOMATIONINTERNAL_PERFORMACTION)

 public:
  struct Result {
    Result();
    Result(const Result&);
    ~Result();
    // If there is a validation error then |automation_error| should be ignored.
    bool validation_success = false;
    // Assuming validation was successful, then a value of base::nullopt
    // implies success. Otherwise, the failure is described in the contained
    // string.
    base::Optional<std::string> automation_error;
  };

  // Exposed to allow crosapi to reuse the implementation. |extension_id| can be
  // the empty string. |extension| and |automation_info| can be nullptr.
  static Result PerformAction(
      const ui::AXTreeID& tree_id,
      int32_t automation_node_id,
      const std::string& action_type,
      int request_id,
      const base::DictionaryValue& additional_properties,
      const std::string& extension_id,
      const Extension* extension,
      const AutomationInfo* automation_info);

 protected:
  ~AutomationInternalPerformActionFunction() override = default;

  ExtensionFunction::ResponseAction Run() override;

 private:
  // Helper function to convert extension action to ax action.
  // |extension_id| can be the empty string.
  // |data| is an out param.
  static Result ConvertToAXActionData(
      const ui::AXTreeID& tree_id,
      int32_t automation_node_id,
      const std::string& action_type,
      int request_id,
      const base::DictionaryValue& additional_properties,
      const std::string& extension_id,
      ui::AXActionData* data);
};

class AutomationInternalEnableTreeFunction : public ExtensionFunction {
  DECLARE_EXTENSION_FUNCTION("automationInternal.enableTree",
                             AUTOMATIONINTERNAL_ENABLETREE)

 public:
  // Returns an error message or base::nullopt on success. Exposed to allow
  // crosapi to reuse the implementation. |extension_id| can be the empty
  // string.
  static base::Optional<std::string> EnableTree(
      const ui::AXTreeID& ax_tree_id,
      const ExtensionId& extension_id);

 protected:
  ~AutomationInternalEnableTreeFunction() override = default;

  ExtensionFunction::ResponseAction Run() override;
};

class AutomationInternalEnableDesktopFunction : public ExtensionFunction {
  DECLARE_EXTENSION_FUNCTION("automationInternal.enableDesktop",
                             AUTOMATIONINTERNAL_ENABLEDESKTOP)
 protected:
  ~AutomationInternalEnableDesktopFunction() override = default;

  ResponseAction Run() override;
};

class AutomationInternalQuerySelectorFunction : public ExtensionFunction {
  DECLARE_EXTENSION_FUNCTION("automationInternal.querySelector",
                             AUTOMATIONINTERNAL_QUERYSELECTOR)

 public:
  using Callback =
      base::OnceCallback<void(const std::string& error, int result_acc_obj_id)>;

 protected:
  ~AutomationInternalQuerySelectorFunction() override = default;

  ResponseAction Run() override;

 private:
  void OnResponse(const std::string& error, int result_acc_obj_id);

  // Used for assigning a unique ID to each request so that the response can be
  // routed appropriately.
  static int query_request_id_counter_;
};

}  // namespace extensions

#endif  // EXTENSIONS_BROWSER_API_AUTOMATION_INTERNAL_AUTOMATION_INTERNAL_API_H_
