// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROMEOS_COMPONENTS_FILE_MANAGER_FILE_MANAGER_PAGE_HANDLER_H_
#define CHROMEOS_COMPONENTS_FILE_MANAGER_FILE_MANAGER_PAGE_HANDLER_H_

#include <memory>

#include "chromeos/components/file_manager/mojom/file_manager.mojom.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"

namespace chromeos {
namespace file_manager {

class FileManagerUI;

// Class backing the page's functionality.
class FileManagerPageHandler : public mojom::PageHandler {
 public:
  FileManagerPageHandler(
      FileManagerUI* file_manager_ui,
      mojo::PendingReceiver<mojom::PageHandler> pending_receiver,
      mojo::PendingRemote<mojom::Page> pending_page);
  ~FileManagerPageHandler() override;

  FileManagerPageHandler(const FileManagerPageHandler&) = delete;
  FileManagerPageHandler& operator=(const FileManagerPageHandler&) = delete;

 private:
  FileManagerUI* file_manager_ui_;  // Owns |this|.
  mojo::Receiver<mojom::PageHandler> receiver_;
  mojo::Remote<mojom::Page> page_;
};

}  // namespace file_manager
}  // namespace chromeos

#endif  // CHROMEOS_COMPONENTS_FILE_MANAGER_FILE_MANAGER_PAGE_HANDLER_H_
