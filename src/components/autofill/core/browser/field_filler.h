// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_AUTOFILL_CORE_BROWSER_FIELD_FILLER_H_
#define COMPONENTS_AUTOFILL_CORE_BROWSER_FIELD_FILLER_H_

#include <string>

#include "base/macros.h"
#include "components/autofill/core/browser/data_model/autofill_profile.h"
#include "components/autofill/core/browser/data_model/credit_card.h"
#include "components/autofill/core/common/form_field_data.h"
#include "third_party/abseil-cpp/absl/types/variant.h"

namespace autofill {

class AddressNormalizer;
class AutofillField;

// Helper class to put user content in fields, to eventually send to the
// renderer.
class FieldFiller {
 public:
  FieldFiller(const std::string& app_locale,
              AddressNormalizer* address_normalizer);
  ~FieldFiller();

  // Set |field_data|'s value to the right value in |profile_or_credit_card|.
  // Uses |field| to determine which field type should be filled, and
  // |app_locale_| as hint when filling exceptional cases like phone number
  // values. Returns |true| if the field has been filled, false otherwise. If
  // |failure_to_fill| is not null, errors are reported to that string.
  bool FillFormField(const AutofillField& field,
                     absl::variant<const AutofillProfile*, const CreditCard*>
                         profile_or_credit_card,
                     FormFieldData* field_data,
                     const std::u16string& cvc,
                     std::string* failure_to_fill = nullptr);

  // Returns the phone number value for the given |field|. The returned value
  // might be |number|, or |phone_home_city_and_number|, or could possibly be a
  // meaningful subset |number|, if that's appropriate for the field.
  static std::u16string GetPhoneNumberValue(
      const AutofillField& field,
      const std::u16string& number,
      const std::u16string& phone_home_city_and_number,
      const FormFieldData& field_data);

  // Returns the index of the shortest entry in the given select field of which
  // |value| is a substring. Returns -1 if no such entry exists.
  static int FindShortestSubstringMatchInSelect(const std::u16string& value,
                                                bool ignore_whitespace,
                                                const FormFieldData* field);

 private:
  const std::string app_locale_;
  // Weak, should outlive this object. May be null.
  AddressNormalizer* address_normalizer_;
};

}  // namespace autofill

#endif  // COMPONENTS_AUTOFILL_CORE_BROWSER_FIELD_FILLER_H_
