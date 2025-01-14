// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_PERMISSIONS_PERMISSION_REQUEST_ID_H_
#define COMPONENTS_PERMISSIONS_PERMISSION_REQUEST_ID_H_

#include <string>

#include "base/util/type_safety/id_type.h"
#include "url/gurl.h"

namespace content {
class RenderFrameHost;
}  // namespace content

namespace permissions {

// Uniquely identifies a particular permission request.
// None of the different attributes (render_process_id, render_frame_id or
// request_local_id) is enough to compare two requests. In order to check if
// a request is the same as another one, consumers of this class should use
// the operator== or operator!=.
class PermissionRequestID {
 public:
  // Uniquely identifies a request (at least) within a given frame.
  using RequestLocalId = util::IdType64<PermissionRequestID>;

  PermissionRequestID(content::RenderFrameHost* render_frame_host,
                      RequestLocalId request_local_id);
  PermissionRequestID(int render_process_id,
                      int render_frame_id,
                      RequestLocalId request_local_id);
  ~PermissionRequestID();

  PermissionRequestID(const PermissionRequestID&);
  PermissionRequestID& operator=(const PermissionRequestID&);

  int render_process_id() const { return render_process_id_; }
  int render_frame_id() const { return render_frame_id_; }

  // Deprecated. Only accessible for testing.
  RequestLocalId request_local_id_for_testing() const {
    return request_local_id_;
  }

  bool operator==(const PermissionRequestID& other) const;
  bool operator!=(const PermissionRequestID& other) const;

  std::string ToString() const;

 private:
  int render_process_id_;
  int render_frame_id_;
  RequestLocalId request_local_id_;
};

}  // namespace permissions

namespace std {
template <>
struct hash<permissions::PermissionRequestID::RequestLocalId>
    : public permissions::PermissionRequestID::RequestLocalId::Hasher {};
}  // namespace std

#endif  // COMPONENTS_PERMISSIONS_PERMISSION_REQUEST_ID_H_
