// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef HEADLESS_LIB_BROWSER_HEADLESS_CONTENT_BROWSER_CLIENT_H_
#define HEADLESS_LIB_BROWSER_HEADLESS_CONTENT_BROWSER_CLIENT_H_

#include <memory>

#include "content/public/browser/content_browser_client.h"
#include "content/public/browser/render_frame_host.h"
#include "headless/public/headless_browser.h"
#include "third_party/blink/public/mojom/badging/badging.mojom.h"

namespace headless {

class HeadlessBrowserImpl;

class HeadlessContentBrowserClient : public content::ContentBrowserClient {
 public:
  explicit HeadlessContentBrowserClient(HeadlessBrowserImpl* browser);
  ~HeadlessContentBrowserClient() override;

  // content::ContentBrowserClient implementation:
  std::unique_ptr<content::BrowserMainParts> CreateBrowserMainParts(
      const content::MainFunctionParams&) override;
  void OverrideWebkitPrefs(content::WebContents* web_contents,
                           blink::web_pref::WebPreferences* prefs) override;
  void RegisterBrowserInterfaceBindersForFrame(
      content::RenderFrameHost* render_frame_host,
      mojo::BinderMapWithContext<content::RenderFrameHost*>* map) override;
  std::unique_ptr<content::DevToolsManagerDelegate>
  CreateDevToolsManagerDelegate() override;
  scoped_refptr<content::QuotaPermissionContext> CreateQuotaPermissionContext()
      override;
  content::GeneratedCodeCacheSettings GetGeneratedCodeCacheSettings(
      content::BrowserContext* context) override;
#if defined(OS_POSIX) && !defined(OS_MAC)
  void GetAdditionalMappedFilesForChildProcess(
      const base::CommandLine& command_line,
      int child_process_id,
      content::PosixFileDescriptorInfo* mappings) override;
#endif
  void AppendExtraCommandLineSwitches(base::CommandLine* command_line,
                                      int child_process_id) override;
  std::string GetApplicationLocale() override;
  std::string GetAcceptLangs(content::BrowserContext* context) override;
  void AllowCertificateError(
      content::WebContents* web_contents,
      int cert_error,
      const net::SSLInfo& ssl_info,
      const GURL& request_url,
      bool is_main_frame_request,
      bool strict_enforcement,
      base::OnceCallback<void(content::CertificateRequestResultType)> callback)
      override;
  base::OnceClosure SelectClientCertificate(
      content::WebContents* web_contents,
      net::SSLCertRequestInfo* cert_request_info,
      net::ClientCertIdentityList client_certs,
      std::unique_ptr<content::ClientCertificateDelegate> delegate) override;
  bool ShouldEnableStrictSiteIsolation() override;

  void ConfigureNetworkContextParams(
      content::BrowserContext* context,
      bool in_memory,
      const base::FilePath& relative_partition_path,
      ::network::mojom::NetworkContextParams* network_context_params,
      ::cert_verifier::mojom::CertVerifierCreationParams*
          cert_verifier_creation_params) override;

  std::string GetProduct() override;
  std::string GetUserAgent() override;

  bool CanAcceptUntrustedExchangesIfNeeded() override;
  device::GeolocationSystemPermissionManager* GetLocationPermissionManager()
      override;

 private:
  class StubBadgeService;

  void BindBadgeService(
      content::RenderFrameHost* render_frame_host,
      mojo::PendingReceiver<blink::mojom::BadgeService> receiver);

  HeadlessBrowserImpl* browser_;  // Not owned.

  // We store the callback here because we may call it from the I/O thread.
  HeadlessBrowser::Options::AppendCommandLineFlagsCallback
      append_command_line_flags_callback_;

  std::unique_ptr<StubBadgeService> stub_badge_service_;

  DISALLOW_COPY_AND_ASSIGN(HeadlessContentBrowserClient);
};

}  // namespace headless

#endif  // HEADLESS_LIB_BROWSER_HEADLESS_CONTENT_BROWSER_CLIENT_H_
