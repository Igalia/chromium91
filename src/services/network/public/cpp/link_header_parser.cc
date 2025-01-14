// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "services/network/public/cpp/link_header_parser.h"

#include <algorithm>
#include <string>
#include <unordered_map>

#include "base/strings/string_util.h"
#include "components/link_header_util/link_header_util.h"
#include "net/base/mime_util.h"
#include "net/http/http_response_headers.h"

namespace network {

namespace {

bool IsValidMimeType(const std::string& type_string) {
  std::string top_level_type;
  std::string subtype;
  if (!net::ParseMimeTypeWithoutParameter(type_string, &top_level_type,
                                          &subtype)) {
    return false;
  }

  return net::IsValidTopLevelMimeType(top_level_type);
}

// Parses `rel` attribute and returns its parsed representation. Returns
// base::nullopt when the value isn't pre-defined.
base::Optional<mojom::LinkRelAttribute> ParseRelAttribute(
    const base::Optional<std::string>& attr) {
  if (!attr.has_value())
    return base::nullopt;

  std::string value = base::ToLowerASCII(attr.value());
  if (value == "preload")
    return mojom::LinkRelAttribute::kPreload;
  return base::nullopt;
}

// Parses `as` attribute and returns its parsed representation. Returns
// base::nullopt when the value isn't pre-defined.
base::Optional<mojom::LinkAsAttribute> ParseAsAttribute(
    const base::Optional<std::string>& attr) {
  if (!attr.has_value())
    return base::nullopt;

  std::string value = base::ToLowerASCII(attr.value());
  if (value == "font")
    return mojom::LinkAsAttribute::kFont;
  else if (value == "image")
    return mojom::LinkAsAttribute::kImage;
  else if (value == "script")
    return mojom::LinkAsAttribute::kScript;
  else if (value == "stylesheet")
    return mojom::LinkAsAttribute::kStyleSheet;
  return base::nullopt;
}

// Parses `crossorigin` attribute and returns its parsed representation. Returns
// base::nullopt when the value isn't pre-defined.
base::Optional<mojom::CrossOriginAttribute> ParseCrossOriginAttribute(
    const base::Optional<std::string>& attr) {
  if (!attr.has_value())
    return mojom::CrossOriginAttribute::kAnonymous;

  std::string value = base::ToLowerASCII(attr.value());
  if (value == "anonymous")
    return mojom::CrossOriginAttribute::kAnonymous;
  else if (value == "use-credentials")
    return mojom::CrossOriginAttribute::kUseCredentials;
  return base::nullopt;
}

// Parses attributes of a Link header and populates parsed representations of
// attributes. Returns true only when all attributes and their values are
// pre-definied.
bool ParseAttributes(
    const std::unordered_map<std::string, base::Optional<std::string>>& attrs,
    mojom::LinkHeaderPtr& parsed) {
  bool is_rel_set = false;

  for (const auto& attr : attrs) {
    std::string name = base::ToLowerASCII(attr.first);

    if (name == "rel") {
      // Ignore if `rel` is already set.
      if (is_rel_set)
        continue;
      base::Optional<mojom::LinkRelAttribute> rel =
          ParseRelAttribute(attr.second);
      if (!rel.has_value())
        return false;
      parsed->rel = rel.value();
      is_rel_set = true;
    } else if (name == "as") {
      // TODO(crbug.com/1182567): Make sure ignoring second and subsequent ones
      // is a reasonable behavior.
      if (parsed->as != mojom::LinkAsAttribute::kUnspecified)
        continue;
      base::Optional<mojom::LinkAsAttribute> as = ParseAsAttribute(attr.second);
      if (!as.has_value())
        return false;
      parsed->as = as.value();
    } else if (name == "crossorigin") {
      // TODO(crbug.com/1182567): Make sure ignoring second and subsequent ones
      // is a reasonable behavior.
      if (parsed->cross_origin != mojom::CrossOriginAttribute::kUnspecified)
        continue;
      base::Optional<mojom::CrossOriginAttribute> cross_origin =
          ParseCrossOriginAttribute(attr.second);
      if (!cross_origin.has_value())
        return false;
      parsed->cross_origin = cross_origin.value();
    } else if (name == "type") {
      // TODO(crbug.com/1182567): Make sure ignoring second and subsequent ones
      // is a reasonable behavior.
      if (parsed->mime_type.has_value())
        continue;
      if (!attr.second.has_value() || !IsValidMimeType(attr.second.value()))
        return false;
      parsed->mime_type = attr.second.value();
    } else {
      // The current Link header contains an attribute which isn't pre-defined.
      return false;
    }
  }

  // `rel` must be present.
  return is_rel_set;
}

}  // namespace

std::vector<mojom::LinkHeaderPtr> ParseLinkHeaders(
    const net::HttpResponseHeaders& headers,
    const GURL& base_url) {
  std::vector<mojom::LinkHeaderPtr> parsed_headers;
  std::string link_header;
  headers.GetNormalizedHeader("link", &link_header);
  for (const auto& pair : link_header_util::SplitLinkHeader(link_header)) {
    std::string url;
    std::unordered_map<std::string, base::Optional<std::string>> attrs;
    if (!link_header_util::ParseLinkHeaderValue(pair.first, pair.second, &url,
                                                &attrs)) {
      continue;
    }

    auto parsed = mojom::LinkHeader::New();

    parsed->href = base_url.Resolve(url);
    if (!parsed->href.is_valid())
      continue;

    if (!ParseAttributes(attrs, parsed))
      continue;

    parsed_headers.push_back(std::move(parsed));
  }

  return parsed_headers;
}

}  // namespace network
