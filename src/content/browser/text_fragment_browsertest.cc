// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/test/scoped_feature_list.h"
#include "base/test/test_timeouts.h"
#include "content/browser/renderer_host/render_widget_host_impl.h"
#include "content/public/browser/render_view_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/content_features.h"
#include "content/public/common/content_switches.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/content_browser_test.h"
#include "content/public/test/content_browser_test_utils.h"
#include "content/public/test/hit_test_region_observer.h"
#include "content/public/test/test_navigation_observer.h"
#include "content/shell/browser/shell.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/controllable_http_response.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "third_party/blink/public/common/switches.h"
#include "url/gurl.h"

// RunUntilInputProcessed will force a Blink lifecycle which is needed
// because did_scroll is set in an onscroll handler which may be delayed from
// the scroll by a frame.
#define EXPECT_DID_SCROLL(scrolled)                        \
  RunUntilInputProcessed(GetWidgetHost());                 \
  EXPECT_EQ(scrolled, EvalJs(main_contents, "did_scroll;", \
                             EXECUTE_SCRIPT_NO_USER_GESTURE));

#define ASSERT_DID_SCROLL(scrolled)                        \
  RunUntilInputProcessed(GetWidgetHost());                 \
  ASSERT_EQ(scrolled, EvalJs(main_contents, "did_scroll;", \
                             EXECUTE_SCRIPT_NO_USER_GESTURE));

namespace content {

class TextFragmentAnchorBrowserTest : public ContentBrowserTest {
 public:
  TextFragmentAnchorBrowserTest() {
    feature_list_.InitAndEnableFeature(features::kDocumentPolicy);
  }

 protected:
  void SetUpOnMainThread() override {
    host_resolver()->AddRule("*", "127.0.0.1");
  }

  void SetUpCommandLine(base::CommandLine* command_line) override {
    ContentBrowserTest::SetUpCommandLine(command_line);
    command_line->AppendSwitchASCII(switches::kEnableBlinkFeatures,
                                    "TextFragmentIdentifiers");
    // Slow bots are flaky due to slower loading interacting with
    // deferred commits.
    command_line->AppendSwitch(blink::switches::kAllowPreCommitInput);
  }

  // Simulates a click on the middle of the DOM element with the given |id|.
  void ClickElementWithId(WebContents* web_contents, const std::string& id) {
    // Get the center coordinates of the DOM element.
    const int x = EvalJs(web_contents,
                         JsReplace("const bounds = "
                                   "document.getElementById($1)."
                                   "getBoundingClientRect();"
                                   "Math.floor(bounds.left + bounds.width / 2)",
                                   id))
                      .ExtractInt();
    const int y = EvalJs(web_contents,
                         JsReplace("const bounds = "
                                   "document.getElementById($1)."
                                   "getBoundingClientRect();"
                                   "Math.floor(bounds.top + bounds.height / 2)",
                                   id))
                      .ExtractInt();

    SimulateMouseClickAt(web_contents, 0, blink::WebMouseEvent::Button::kLeft,
                         gfx::Point(x, y));
  }

  void WaitForPageLoad(WebContents* contents) {
    EXPECT_TRUE(WaitForLoadStop(contents));
    EXPECT_TRUE(WaitForRenderFrameReady(contents->GetMainFrame()));
  }

  RenderWidgetHostImpl* GetWidgetHost() {
    return RenderWidgetHostImpl::From(shell()
                                          ->web_contents()
                                          ->GetMainFrame()
                                          ->GetRenderViewHost()
                                          ->GetWidget());
  }

  base::test::ScopedFeatureList feature_list_;
};

IN_PROC_BROWSER_TEST_F(TextFragmentAnchorBrowserTest, EnabledOnUserNavigation) {
  ASSERT_TRUE(embedded_test_server()->Start());
  GURL url(embedded_test_server()->GetURL("/target_text_link.html"));
  GURL target_text_url(embedded_test_server()->GetURL(
      "/scrollable_page_with_content.html#:~:text=text"));

  EXPECT_TRUE(NavigateToURL(shell(), url));

  WebContents* main_contents = shell()->web_contents();
  TestNavigationObserver observer(main_contents);

  // We need to wait until hit test data is available.
  HitTestRegionObserver hittest_observer(GetWidgetHost()->GetFrameSinkId());
  hittest_observer.WaitForHitTestData();

  ClickElementWithId(main_contents, "link");
  observer.Wait();
  EXPECT_EQ(target_text_url, main_contents->GetLastCommittedURL());

  // Observe the frame after page is loaded. Note that we need to initialize
  // this after navigation because the main RenderFrameHost might have changed
  // from before the navigation started.
  RenderFrameSubmissionObserver frame_observer(main_contents);
  WaitForPageLoad(main_contents);
  frame_observer.WaitForScrollOffsetAtTop(
      /*expected_scroll_offset_at_top=*/false);

  EXPECT_DID_SCROLL(true);
}

IN_PROC_BROWSER_TEST_F(TextFragmentAnchorBrowserTest,
                       EnabledOnBrowserNavigation) {
  ASSERT_TRUE(embedded_test_server()->Start());
  GURL url(embedded_test_server()->GetURL(
      "/scrollable_page_with_content.html#:~:text=text"));
  WebContents* main_contents = shell()->web_contents();
  RenderFrameSubmissionObserver frame_observer(main_contents);

  EXPECT_TRUE(NavigateToURL(shell(), url));

  WaitForPageLoad(main_contents);
  frame_observer.WaitForScrollOffsetAtTop(
      /*expected_scroll_offset_at_top=*/false);
  EXPECT_DID_SCROLL(true);
}

IN_PROC_BROWSER_TEST_F(TextFragmentAnchorBrowserTest,
                       EnabledOnUserGestureScriptNavigation) {
  ASSERT_TRUE(embedded_test_server()->Start());
  GURL url(embedded_test_server()->GetURL("/empty.html"));
  GURL target_text_url(embedded_test_server()->GetURL(
      "/scrollable_page_with_content.html#:~:text=text"));

  EXPECT_TRUE(NavigateToURL(shell(), url));

  WebContents* main_contents = shell()->web_contents();
  TestNavigationObserver observer(main_contents);

  // ExecuteScript executes with a user gesture
  EXPECT_TRUE(ExecuteScript(main_contents,
                            "location = '" + target_text_url.spec() + "';"));
  observer.Wait();
  EXPECT_EQ(target_text_url, main_contents->GetLastCommittedURL());
  // Observe the frame after page is loaded. Note that we need to initialize
  // this after navigation because the main RenderFrameHost might have changed
  // from before the navigation started.
  RenderFrameSubmissionObserver frame_observer(main_contents);

  WaitForPageLoad(main_contents);
  frame_observer.WaitForScrollOffsetAtTop(
      /*expected_scroll_offset_at_top=*/false);
  EXPECT_DID_SCROLL(true);
}

// Ensures that a simulated redirect service works correctly. That is, only the
// initial NavigateToURL has a user gesture but this should be propagated
// through the window.location navigation which doesn't have a user gesture.
IN_PROC_BROWSER_TEST_F(TextFragmentAnchorBrowserTest,
                       UserGesturePassedThroughRedirect) {
  ASSERT_TRUE(embedded_test_server()->Start());
  GURL url(embedded_test_server()->GetURL("/empty.html"));
  GURL target_text_url(embedded_test_server()->GetURL(
      "/scrollable_page_with_content.html#:~:text=text"));

  // This navigtion is simulated as if it came from the omnibox, hence it is
  // considered to be user initiated.
  EXPECT_TRUE(NavigateToURL(shell(), url));

  WebContents* main_contents = shell()->web_contents();
  TestNavigationObserver observer(main_contents);

  // This navigation occurs without a user gesture, simulating a client
  // redirect. However, because the above navigation didn't activate a text
  // fragment, permission should be propagated to this navigation.
  EXPECT_TRUE(ExecuteScriptWithoutUserGesture(
      main_contents, "location = '" + target_text_url.spec() + "';"));
  observer.Wait();
  EXPECT_EQ(target_text_url, main_contents->GetLastCommittedURL());

  WaitForPageLoad(main_contents);
  RenderFrameSubmissionObserver frame_observer(main_contents);
  frame_observer.WaitForScrollOffsetAtTop(
      /*expected_scroll_offset_at_top=*/false);
  EXPECT_DID_SCROLL(true);
}

// Ensures that a text fragment activation consumes a user gesture so that
// future navigations cannot activate a text fragment without a new user
// gesture.
IN_PROC_BROWSER_TEST_F(TextFragmentAnchorBrowserTest, UserGestureConsumed) {
  ASSERT_TRUE(embedded_test_server()->Start());
  GURL empty_page_url(embedded_test_server()->GetURL("/empty.html"));
  GURL target_text_url(embedded_test_server()->GetURL(
      "/scrollable_page_with_content.html#:~:text=text"));

  WebContents* main_contents = shell()->web_contents();

  // This navigtion is simulated as if it came from the omnibox, hence it is
  // considered to be user initiated.
  {
    TestNavigationObserver observer(main_contents);
    ASSERT_TRUE(NavigateToURL(shell(), target_text_url));
    observer.Wait();
    ASSERT_EQ(target_text_url, main_contents->GetLastCommittedURL());

    // Ensure the page did scroll to the text fragment. Note, we can't use
    // WaitForPageLoad since the WaitForRenderFrameReady executes javascript
    // with a user gesture.
    WaitForLoadStop(main_contents);
    RenderFrameSubmissionObserver frame_observer(main_contents);
    frame_observer.WaitForScrollOffsetAtTop(
        /*expected_scroll_offset_at_top=*/false);
    ASSERT_DID_SCROLL(true);
  }

  // We now want to try a second text fragment navigation. Same document
  // navigations are blocked so we'll navigate away first.
  {
    TestNavigationObserver observer(main_contents);
    ASSERT_TRUE(ExecuteScriptWithoutUserGesture(
        main_contents, "location = '" + empty_page_url.spec() + "';"));
    observer.Wait();
    ASSERT_EQ(empty_page_url, main_contents->GetLastCommittedURL());
    WaitForLoadStop(main_contents);
  }

  // Now try another text fragment navigation. Since we haven't had a user
  // gesture since the last one, it should be blocked.
  {
    TestNavigationObserver observer(main_contents);
    ASSERT_TRUE(ExecuteScriptWithoutUserGesture(
        main_contents, "location = '" + target_text_url.spec() + "';"));
    observer.Wait();
    ASSERT_EQ(target_text_url, main_contents->GetLastCommittedURL());
    WaitForLoadStop(main_contents);

    // Wait a short amount of time to ensure the page does not scroll.
    base::RunLoop run_loop;
    base::ThreadTaskRunnerHandle::Get()->PostDelayedTask(
        FROM_HERE, run_loop.QuitClosure(), TestTimeouts::tiny_timeout());
    run_loop.Run();
    EXPECT_DID_SCROLL(false);
  }
}

IN_PROC_BROWSER_TEST_F(TextFragmentAnchorBrowserTest,
                       DisabledOnScriptHistoryNavigation) {
  ASSERT_TRUE(embedded_test_server()->Start());
  GURL target_text_url(embedded_test_server()->GetURL(
      "/scrollable_page_with_content.html#:~:text=text"));
  GURL url(embedded_test_server()->GetURL("/empty.html"));

  EXPECT_TRUE(NavigateToURL(shell(), target_text_url));

  WebContents* main_contents = shell()->web_contents();
  {
    // The RenderFrameSubmissionObserver destructor expects the RenderFrameHost
    // stays the same until it gets destructed, so we need to scope this to make
    // sure it gets destructed before the next navigation.
    RenderFrameSubmissionObserver frame_observer(main_contents);
    frame_observer.WaitForScrollOffsetAtTop(false);

    // Scroll the page back to top so scroll restoration does not scroll the
    // target back into view.
    EXPECT_TRUE(ExecuteScript(main_contents, "window.scrollTo(0, 0)"));
    frame_observer.WaitForScrollOffsetAtTop(true);
  }

  EXPECT_TRUE(NavigateToURL(shell(), url));

  TestNavigationObserver observer(main_contents);
  EXPECT_TRUE(ExecuteScriptWithoutUserGesture(main_contents, "history.back()"));
  observer.Wait();
  EXPECT_EQ(target_text_url, main_contents->GetLastCommittedURL());

  WaitForPageLoad(main_contents);

  // Wait a short amount of time to ensure the page does not scroll.
  base::RunLoop run_loop;
  base::ThreadTaskRunnerHandle::Get()->PostDelayedTask(
      FROM_HERE, run_loop.QuitClosure(), TestTimeouts::tiny_timeout());
  run_loop.Run();

  // Note: we use a scroll handler in the page to check whether any scrolls
  // happened at all, rather than checking the current scroll offset. This is
  // to ensure that if the offset is reset back to the top for other reasons
  // (e.g. history restoration) we still fail this test. See
  // https://crbug.com/1042986 for why this matters.
  EXPECT_DID_SCROLL(false);
}

// Normally, same document navigations don't allow invoking the text fragment.
// We make an exception for browser-initiated (e.g. typing a URL into the
// omnibox) navigations. This test ensures we allow the latter.
IN_PROC_BROWSER_TEST_F(TextFragmentAnchorBrowserTest,
                       EnabledOnSameDocumentBrowserNavigation) {
  ASSERT_TRUE(embedded_test_server()->Start());
  GURL url(embedded_test_server()->GetURL(
      "/scrollable_page_with_content.html#:~:text=text"));
  WebContents* main_contents = shell()->web_contents();
  RenderFrameSubmissionObserver frame_observer(main_contents);

  EXPECT_TRUE(NavigateToURL(shell(), url));

  WaitForPageLoad(main_contents);
  frame_observer.WaitForScrollOffsetAtTop(false);

  // Scroll the page back to top. Make sure we reset the |did_scroll| variable
  // we'll use below to ensure the same-document navigation invokes the text
  // fragment.
  EXPECT_TRUE(ExecuteScript(main_contents, "window.scrollTo(0, 0)"));
  frame_observer.WaitForScrollOffsetAtTop(true);
  RunUntilInputProcessed(GetWidgetHost());
  EXPECT_TRUE(ExecJs(main_contents, "did_scroll = false;"));

  // Perform a same-document browser initiated navigation
  GURL same_doc_url(embedded_test_server()->GetURL(
      "/scrollable_page_with_content.html#:~:text=some"));
  EXPECT_TRUE(NavigateToURL(shell(), same_doc_url));

  WaitForPageLoad(main_contents);
  frame_observer.WaitForScrollOffsetAtTop(
      /*expected_scroll_offset_at_top=*/false);
  EXPECT_DID_SCROLL(true);
}

// Similar to the above test, we're checking that browser-initiated
// same-document navigations invoke the text fragment. However, this time, the
// initial landing on the page is via a non-user-activated script navigation.
// This ensure we're not inappropriately blocking a text-fragment based on the
// state of the initial document load.
IN_PROC_BROWSER_TEST_F(TextFragmentAnchorBrowserTest,
                       SameDocumentBrowserNavigationOnScriptNavigatedDocument) {
  ASSERT_TRUE(embedded_test_server()->Start());
  WebContents* main_contents = shell()->web_contents();
  RenderFrameSubmissionObserver frame_observer(main_contents);

  // Load an initial page
  {
    GURL initial_url(embedded_test_server()->GetURL("/empty.html"));
    EXPECT_TRUE(NavigateToURL(shell(), initial_url));
    WaitForPageLoad(main_contents);
  }

  // Now navigate to the target document without a user gesture. We provide a
  // text-fragment here and expect it to be invoked because the initial load
  // was browser-initiated so its transferred to this load via the text fragment
  // token. This navigation ensures the token is consumed.
  {
    GURL target_url(embedded_test_server()->GetURL(
        "/scrollable_page_with_content.html#:~:text=text"));
    TestNavigationObserver observer(main_contents);
    EXPECT_TRUE(ExecuteScriptWithoutUserGesture(
        main_contents, "location = '" + target_url.spec() + "';"));
    observer.Wait();
    EXPECT_EQ(target_url, main_contents->GetLastCommittedURL());
    frame_observer.WaitForScrollOffsetAtTop(false);
    EXPECT_DID_SCROLL(true);
  }

  // Scroll the page back to top. Make sure we reset the |did_scroll| variable
  // we'll use below to ensure the same-document navigation invokes the text
  // fragment.
  {
    EXPECT_TRUE(ExecuteScript(main_contents, "window.scrollTo(0, 0)"));
    frame_observer.WaitForScrollOffsetAtTop(true);
    RunUntilInputProcessed(GetWidgetHost());
    EXPECT_TRUE(ExecJs(main_contents, "did_scroll = false;"));
  }

  // Perform a same-document browser initiated navigation. This should cause a
  // scroll because the navigation is browser-initiated, despite the fact that
  // the document was loaded without a user gesture.
  {
    GURL same_doc_url(embedded_test_server()->GetURL(
        "/scrollable_page_with_content.html#:~:text=some"));
    EXPECT_TRUE(NavigateToURL(shell(), same_doc_url));

    WaitForPageLoad(main_contents);

    frame_observer.WaitForScrollOffsetAtTop(
        /*expected_scroll_offset_at_top=*/false);
    EXPECT_DID_SCROLL(true);
  }
}

// Ensure a text fragment token isn't generated via history.back() navigation.
// This is a tricky case because all history navigations (including script
// initiated) appear to the renderer as "browser-initiated".
IN_PROC_BROWSER_TEST_F(TextFragmentAnchorBrowserTest,
                       HistoryDoesntGenerateToken) {
  ASSERT_TRUE(embedded_test_server()->Start());
  WebContents* main_contents = shell()->web_contents();
  RenderFrameSubmissionObserver frame_observer(main_contents);
  GURL url(embedded_test_server()->GetURL(
      "/scrollable_page_with_content.html#:~:text=text"));

  // Load a page with a text-fragment
  {
    EXPECT_TRUE(NavigateToURL(shell(), url));
    WaitForPageLoad(main_contents);
    frame_observer.WaitForScrollOffsetAtTop(false);
  }

  // Scroll the page back to top. Make sure we reset the |did_scroll| variable
  // we'll use below to ensure the same-document navigation invokes the text
  // fragment.
  {
    EXPECT_TRUE(ExecuteScript(main_contents, "window.scrollTo(0, 0)"));
    frame_observer.WaitForScrollOffsetAtTop(true);
    RunUntilInputProcessed(GetWidgetHost());
    EXPECT_TRUE(ExecJs(main_contents, "did_scroll = false;"));
  }

  // Perform a scripted same-document navigation to a non-existent fragment to
  // generate a history entry.
  {
    GURL temp_url(embedded_test_server()->GetURL(
        "/scrollable_page_with_content.html#doesntexist"));
    TestNavigationObserver observer(main_contents);
    EXPECT_TRUE(ExecuteScriptWithoutUserGesture(
        main_contents, "location = '" + temp_url.spec() + "';"));
    observer.Wait();
    EXPECT_EQ(temp_url, main_contents->GetLastCommittedURL());
  }

  // Navigate back using history.back().
  {
    TestNavigationObserver observer(main_contents);
    EXPECT_TRUE(
        ExecuteScriptWithoutUserGesture(main_contents, "history.back();"));
    observer.Wait();
    EXPECT_EQ(url, main_contents->GetLastCommittedURL());

    // The page should be restored to where we left off at the top.
    RunUntilInputProcessed(GetWidgetHost());
    ASSERT_TRUE(
        frame_observer.LastRenderFrameMetadata().is_scroll_offset_at_top);
    ASSERT_DID_SCROLL(false);
  }

  // Now try to navigate to a same-document text-fragment. This should be
  // blocked because the token was consumed in the initial load at the top and
  // a new one should not have been generated by the same document navigations
  // above.
  {
    GURL new_url(embedded_test_server()->GetURL(
        "/scrollable_page_with_content.html#:~:text=Some"));
    TestNavigationObserver observer(main_contents);
    EXPECT_TRUE(ExecuteScriptWithoutUserGesture(
        main_contents, "location = '" + new_url.spec() + "';"));
    observer.Wait();
    EXPECT_EQ(new_url, main_contents->GetLastCommittedURL());
    frame_observer.WaitForScrollOffsetAtTop(true);
    EXPECT_DID_SCROLL(false);
  }
}

IN_PROC_BROWSER_TEST_F(TextFragmentAnchorBrowserTest,
                       DisabledOnSameDocumentScriptNavigation) {
  ASSERT_TRUE(embedded_test_server()->Start());
  GURL url(
      embedded_test_server()->GetURL("/scrollable_page_with_content.html"));
  GURL target_text_url(embedded_test_server()->GetURL(
      "/scrollable_page_with_content.html#:~:text=some"));

  EXPECT_TRUE(NavigateToURL(shell(), url));

  WebContents* main_contents = shell()->web_contents();
  TestNavigationObserver observer(main_contents);
  EXPECT_TRUE(ExecuteScriptWithoutUserGesture(
      main_contents, "location = '" + target_text_url.spec() + "';"));
  observer.Wait();
  EXPECT_EQ(target_text_url, main_contents->GetLastCommittedURL());

  WaitForPageLoad(main_contents);

  // Wait a short amount of time to ensure the page does not scroll.
  base::RunLoop run_loop;
  base::ThreadTaskRunnerHandle::Get()->PostDelayedTask(
      FROM_HERE, run_loop.QuitClosure(), TestTimeouts::tiny_timeout());
  run_loop.Run();
  EXPECT_DID_SCROLL(false);
}

IN_PROC_BROWSER_TEST_F(TextFragmentAnchorBrowserTest, EnabledByDocumentPolicy) {
  net::test_server::ControllableHttpResponse response(embedded_test_server(),
                                                      "/target.html");

  ASSERT_TRUE(embedded_test_server()->Start());
  GURL url(embedded_test_server()->GetURL("/target.html#:~:text=text"));
  WebContents* main_contents = shell()->web_contents();
  RenderFrameSubmissionObserver frame_observer(main_contents);

  // Load the target document
  TestNavigationManager navigation_manager(main_contents, url);
  shell()->LoadURL(url);

  // Start navigation
  EXPECT_TRUE(navigation_manager.WaitForRequestStart());
  navigation_manager.ResumeNavigation();

  // Send Document-Policy header
  response.WaitForRequest();
  response.Send(
      "HTTP/1.1 200 OK\r\n"
      "Content-Type: text/html; charset=utf-8\r\n"
      "Document-Policy: force-load-at-top=?0\r\n"
      "\r\n"
      "<script>"
      "  let did_scroll = false;"
      "  window.addEventListener('scroll', () => {"
      "    did_scroll = true;"
      "  });"
      "</script>"
      "<p style='position: absolute; top: 10000px;'>Some text</p>");
  response.Done();

  EXPECT_TRUE(navigation_manager.WaitForResponse());
  navigation_manager.ResumeNavigation();
  navigation_manager.WaitForNavigationFinished();

  WaitForPageLoad(main_contents);
  frame_observer.WaitForScrollOffsetAtTop(
      /*expected_scroll_offset_at_top=*/false);
  EXPECT_DID_SCROLL(true);
}

IN_PROC_BROWSER_TEST_F(TextFragmentAnchorBrowserTest,
                       DisabledByDocumentPolicy) {
  net::test_server::ControllableHttpResponse response(embedded_test_server(),
                                                      "/target.html");

  ASSERT_TRUE(embedded_test_server()->Start());
  GURL url(embedded_test_server()->GetURL("/target.html#:~:text=text"));
  WebContents* main_contents = shell()->web_contents();

  // Load the target document
  TestNavigationManager navigation_manager(main_contents, url);
  shell()->LoadURL(url);

  // Start navigation
  EXPECT_TRUE(navigation_manager.WaitForRequestStart());
  navigation_manager.ResumeNavigation();

  // Send Document-Policy header
  response.WaitForRequest();
  response.Send(
      "HTTP/1.1 200 OK\r\n"
      "Content-Type: text/html; charset=utf-8\r\n"
      "Document-Policy: force-load-at-top\r\n"
      "\r\n"
      "<script>"
      "  let did_scroll = false;"
      "  window.addEventListener('scroll', () => {"
      "    did_scroll = true;"
      "  });"
      "</script>"
      "<p style='position: absolute; top: 10000px;'>Some text</p>");
  response.Done();

  EXPECT_TRUE(navigation_manager.WaitForResponse());
  navigation_manager.ResumeNavigation();
  navigation_manager.WaitForNavigationFinished();

  WaitForPageLoad(main_contents);
  // Wait a short amount of time to ensure the page does not scroll.
  base::RunLoop run_loop;
  base::ThreadTaskRunnerHandle::Get()->PostDelayedTask(
      FROM_HERE, run_loop.QuitClosure(), TestTimeouts::tiny_timeout());
  run_loop.Run();
  EXPECT_DID_SCROLL(false);
}

// Test that Tab key press puts focus from the start of selection.
IN_PROC_BROWSER_TEST_F(TextFragmentAnchorBrowserTest, TabFocus) {
  ASSERT_TRUE(embedded_test_server()->Start());
  GURL url(embedded_test_server()->GetURL(
      "/scrollable_page_with_anchor.html#:~:text=text"));
  WebContents* main_contents = shell()->web_contents();
  RenderFrameSubmissionObserver frame_observer(main_contents);
  EXPECT_TRUE(NavigateToURL(shell(), url));
  WaitForPageLoad(main_contents);
  frame_observer.WaitForScrollOffsetAtTop(
      /*expected_scroll_offset_at_top=*/false);

  DOMMessageQueue msg_queue;
  SimulateKeyPress(main_contents, ui::DomKey::TAB, ui::DomCode::TAB,
                   ui::VKEY_TAB, false, false, false, false);

  // Wait for focus to happen.
  std::string message;
  EXPECT_TRUE(msg_queue.WaitForMessage(&message));
  EXPECT_EQ("\"FocusDone2\"", message);
}

class ForceLoadAtTopBrowserTest : public TextFragmentAnchorBrowserTest {
 protected:
  // Loads the given path as predetermined HTML response with a
  // |Document-Policy: force-load-at-top| header and waits for the navigation
  // to finish.
  void LoadScrollablePageWithContent(const std::string& path) {
    std::size_t hash_pos = path.find("#");
    std::string path_without_fragment = path;
    if (hash_pos != std::string::npos) {
      path_without_fragment = path.substr(0, hash_pos);
    }
    net::test_server::ControllableHttpResponse response(embedded_test_server(),
                                                        path_without_fragment);

    ASSERT_TRUE(embedded_test_server()->Start());
    GURL url(embedded_test_server()->GetURL(path));
    RenderFrameSubmissionObserver frame_observer(shell()->web_contents());

    // Load the target document.
    TestNavigationManager navigation_manager(shell()->web_contents(), url);
    shell()->LoadURL(url);

    // Start navigation
    ASSERT_TRUE(navigation_manager.WaitForRequestStart());
    navigation_manager.ResumeNavigation();

    // Send Document-Policy header
    response.WaitForRequest();
    std::string response_string =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/html; charset=utf-8\r\n"
        "Document-Policy: force-load-at-top\r\n"
        "\r\n"
        R"HTML(
          <html>
            <head>
              <meta name="viewport" content="width=device-width">
              <script>
                let did_scroll = false;
                window.addEventListener('scroll', () => {
                  did_scroll = true;
                });
              </script>
              <style>
                p {
                  position: absolute;
                  top: 10000px;
                }
              </style>
            </head>
            <body>
              <a id="link" href="#text">Go Down</a>
              <p id="text">Some text</p>
            </body>
          </html>
        )HTML";
    response.Send(response_string);
    response.Done();

    ASSERT_TRUE(navigation_manager.WaitForResponse());
    navigation_manager.ResumeNavigation();
    navigation_manager.WaitForNavigationFinished();
  }
};

// Test that scroll restoration is disabled with ForceLoadAtTop
IN_PROC_BROWSER_TEST_F(ForceLoadAtTopBrowserTest, ScrollRestorationDisabled) {
  ASSERT_NO_FATAL_FAILURE(LoadScrollablePageWithContent("/index.html"));

  WebContents* main_contents = shell()->web_contents();
  RenderFrameSubmissionObserver frame_observer(main_contents);

  EXPECT_TRUE(WaitForRenderFrameReady(main_contents->GetMainFrame()));

  // Scroll down the page a bit
  EXPECT_TRUE(ExecuteScript(main_contents, "window.scrollTo(0, 1000)"));
  frame_observer.WaitForScrollOffsetAtTop(false);

  // Navigate away
  EXPECT_TRUE(ExecuteScript(main_contents, "window.location = 'about:blank'"));
  EXPECT_TRUE(WaitForLoadStop(main_contents));
  EXPECT_TRUE(WaitForRenderFrameReady(main_contents->GetMainFrame()));

  // Navigate back
  EXPECT_TRUE(ExecuteScript(main_contents, "history.back()"));
  EXPECT_TRUE(WaitForLoadStop(main_contents));
  EXPECT_TRUE(WaitForRenderFrameReady(main_contents->GetMainFrame()));

  // Wait a short amount of time to ensure the page does not scroll.
  base::RunLoop run_loop;
  base::ThreadTaskRunnerHandle::Get()->PostDelayedTask(
      FROM_HERE, run_loop.QuitClosure(), TestTimeouts::tiny_timeout());
  run_loop.Run();
  RunUntilInputProcessed(GetWidgetHost());
  const cc::RenderFrameMetadata& last_metadata =
      RenderFrameSubmissionObserver(main_contents).LastRenderFrameMetadata();
  EXPECT_TRUE(last_metadata.is_scroll_offset_at_top);
}

// Test that element fragment anchor scrolling is disabled with ForceLoadAtTop
IN_PROC_BROWSER_TEST_F(ForceLoadAtTopBrowserTest, FragmentAnchorDisabled) {
  ASSERT_NO_FATAL_FAILURE(LoadScrollablePageWithContent("/index.html#text"));
  WebContents* main_contents = shell()->web_contents();

  EXPECT_TRUE(WaitForRenderFrameReady(main_contents->GetMainFrame()));

  // Wait a short amount of time to ensure the page does not scroll.
  base::RunLoop run_loop;
  base::ThreadTaskRunnerHandle::Get()->PostDelayedTask(
      FROM_HERE, run_loop.QuitClosure(), TestTimeouts::tiny_timeout());
  run_loop.Run();
  RunUntilInputProcessed(GetWidgetHost());
  const cc::RenderFrameMetadata& last_metadata =
      RenderFrameSubmissionObserver(main_contents).LastRenderFrameMetadata();
  EXPECT_TRUE(last_metadata.is_scroll_offset_at_top);
}

IN_PROC_BROWSER_TEST_F(ForceLoadAtTopBrowserTest, SameDocumentNavigation) {
  ASSERT_NO_FATAL_FAILURE(LoadScrollablePageWithContent("/index.html"));
  WebContents* main_contents = shell()->web_contents();

  EXPECT_TRUE(WaitForRenderFrameReady(main_contents->GetMainFrame()));
  {
    const cc::RenderFrameMetadata& last_metadata =
        RenderFrameSubmissionObserver(main_contents).LastRenderFrameMetadata();
    EXPECT_TRUE(last_metadata.is_scroll_offset_at_top);
  }

  ClickElementWithId(main_contents, "link");

  RunUntilInputProcessed(GetWidgetHost());
  {
    const cc::RenderFrameMetadata& last_metadata =
        RenderFrameSubmissionObserver(main_contents).LastRenderFrameMetadata();
    EXPECT_FALSE(last_metadata.is_scroll_offset_at_top);
  }
}

IN_PROC_BROWSER_TEST_F(ForceLoadAtTopBrowserTest, TextFragmentAnchorDisabled) {
  ASSERT_NO_FATAL_FAILURE(
      LoadScrollablePageWithContent("/index.html#:~:text=text"));
  WebContents* main_contents = shell()->web_contents();
  RenderFrameSubmissionObserver frame_observer(main_contents);

  EXPECT_TRUE(WaitForRenderFrameReady(main_contents->GetMainFrame()));

  // Wait a short amount of time to ensure the page does not scroll.
  base::RunLoop run_loop;
  base::ThreadTaskRunnerHandle::Get()->PostDelayedTask(
      FROM_HERE, run_loop.QuitClosure(), TestTimeouts::tiny_timeout());
  run_loop.Run();
  RunUntilInputProcessed(GetWidgetHost());
  const cc::RenderFrameMetadata& last_metadata =
      RenderFrameSubmissionObserver(main_contents).LastRenderFrameMetadata();
  EXPECT_TRUE(last_metadata.is_scroll_offset_at_top);
}

}  // namespace content
