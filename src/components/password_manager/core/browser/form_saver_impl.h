// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_PASSWORD_MANAGER_CORE_BROWSER_FORM_SAVER_IMPL_H_
#define COMPONENTS_PASSWORD_MANAGER_CORE_BROWSER_FORM_SAVER_IMPL_H_

#include <memory>

#include "base/macros.h"
#include "components/password_manager/core/browser/form_saver.h"

namespace password_manager {

class PasswordStore;

// The production code implementation of FormSaver.
class FormSaverImpl : public FormSaver {
 public:
  // |store| needs to outlive |this| and will be used for all PasswordStore
  // operations.
  explicit FormSaverImpl(PasswordStore* store);

  ~FormSaverImpl() override;

  // FormSaver:
  PasswordForm Blocklist(PasswordStore::FormDigest digest) override;
  void Unblocklist(const PasswordStore::FormDigest& digest) override;
  void Save(PasswordForm pending,
            const std::vector<const PasswordForm*>& matches,
            const std::u16string& old_password) override;
  void Update(PasswordForm pending,
              const std::vector<const PasswordForm*>& matches,
              const std::u16string& old_password) override;
  void UpdateReplace(PasswordForm pending,
                     const std::vector<const PasswordForm*>& matches,
                     const std::u16string& old_password,
                     const PasswordForm& old_unique_key) override;
  void Remove(const PasswordForm& form) override;
  std::unique_ptr<FormSaver> Clone() override;

 private:
  // The class is stateless. Don't introduce it. The methods are utilities for
  // common tasks on the password store. The state should belong to either a
  // form handler or origin handler which could embed FormSaver.

  // Cached pointer to the PasswordStore.
  PasswordStore* const store_;

  DISALLOW_COPY_AND_ASSIGN(FormSaverImpl);
};

}  // namespace password_manager

#endif  // COMPONENTS_PASSWORD_MANAGER_CORE_BROWSER_FORM_SAVER_IMPL_H_
