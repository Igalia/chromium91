// Copyright (c) 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/files/file_util.h"
#include "content/browser/accessibility/dump_accessibility_browsertest_base.h"
#include "content/browser/web_contents/web_contents_impl.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/content_browser_test_utils.h"
#include "content/shell/browser/shell.h"
#include "net/base/escape.h"
#include "ui/accessibility/accessibility_switches.h"

namespace content {

using ui::AXPropertyFilter;
using ui::AXTreeFormatter;

class DumpAccessibilityNodeTest : public DumpAccessibilityTestBase {
 public:
  void SetUpCommandLine(base::CommandLine* command_line) override {
    // kDisableAXMenuList is true on Chrome OS by default. This can cause the
    // calculation of text alternatives from content to fail in blink tests
    // which include a select element descendant.
    base::CommandLine::ForCurrentProcess()->AppendSwitchASCII(
        switches::kDisableAXMenuList, "false");
  }

  std::vector<ui::AXPropertyFilter> DefaultFilters() const override {
    std::vector<AXPropertyFilter> property_filters;
    property_filters.emplace_back("value='*'", AXPropertyFilter::ALLOW);
    property_filters.emplace_back("value='http*'", AXPropertyFilter::DENY);
    property_filters.emplace_back("layout-guess:*", AXPropertyFilter::ALLOW);
    property_filters.emplace_back("select*", AXPropertyFilter::ALLOW);
    property_filters.emplace_back("selectedFromFocus=*",
                                  AXPropertyFilter::DENY);
    property_filters.emplace_back("descript*", AXPropertyFilter::ALLOW);
    property_filters.emplace_back("check*", AXPropertyFilter::ALLOW);
    property_filters.emplace_back("horizontal", AXPropertyFilter::ALLOW);
    property_filters.emplace_back("multiselectable", AXPropertyFilter::ALLOW);
    property_filters.emplace_back("placeholder=*", AXPropertyFilter::ALLOW);
    property_filters.emplace_back("*=''", AXPropertyFilter::DENY);
    property_filters.emplace_back("name=*", AXPropertyFilter::ALLOW_EMPTY);
    return property_filters;
  }

  std::vector<std::string> Dump(std::vector<std::string>& unused) override {
    std::unique_ptr<AXTreeFormatter> formatter(CreateFormatter());

    formatter->SetPropertyFilters(scenario_.property_filters,
                                  AXTreeFormatter::kFiltersDefaultSet);

    BrowserAccessibility* test_node = FindNodeByHTMLAttribute("id", "test");
    if (!test_node)
      test_node = FindNodeByHTMLAttribute("class", "test");

    std::string contents =
        test_node ? formatter->FormatNode(test_node) : "Test node not found.";

    std::string escaped_contents = net::EscapeNonASCII(contents);
    return base::SplitString(escaped_contents, "\n", base::KEEP_WHITESPACE,
                             base::SPLIT_WANT_NONEMPTY);
  }

  void RunAriaTest(const base::FilePath::CharType* file_path) {
    base::FilePath test_path = GetTestFilePath("accessibility", "aria");
    {
      base::ScopedAllowBlockingForTesting allow_blocking;
      ASSERT_TRUE(base::PathExists(test_path)) << test_path.LossyDisplayName();
    }
    base::FilePath aria_file = test_path.Append(base::FilePath(file_path));
    RunTest(aria_file, "accessibility/aria", FILE_PATH_LITERAL("node"));
  }

  void RunHtmlTest(const base::FilePath::CharType* file_path) {
    base::FilePath test_path = GetTestFilePath("accessibility", "html");
    {
      base::ScopedAllowBlockingForTesting allow_blocking;
      ASSERT_TRUE(base::PathExists(test_path)) << test_path.LossyDisplayName();
    }
    base::FilePath html_file = test_path.Append(base::FilePath(file_path));
    RunTest(html_file, "accessibility/html", FILE_PATH_LITERAL("node"));
  }
};

class DumpAccessibilityAccNameTest : public DumpAccessibilityNodeTest {
 public:
  std::vector<ui::AXPropertyFilter> DefaultFilters() const override {
    std::vector<AXPropertyFilter> property_filters;
    property_filters.emplace_back("name*", AXPropertyFilter::ALLOW_EMPTY);
    property_filters.emplace_back("description*",
                                  AXPropertyFilter::ALLOW_EMPTY);

    // Since we normally care about the name and/or description, filter out
    // various irrelevant states and properties.
    property_filters.emplace_back("checkable", AXPropertyFilter::DENY);
    property_filters.emplace_back("clickable", AXPropertyFilter::DENY);
    property_filters.emplace_back("editable*", AXPropertyFilter::DENY);
    property_filters.emplace_back("focusable", AXPropertyFilter::DENY);
    property_filters.emplace_back("horizontal", AXPropertyFilter::DENY);
    property_filters.emplace_back("multiselectable", AXPropertyFilter::DENY);
    property_filters.emplace_back("vertical", AXPropertyFilter::DENY);
    return property_filters;
  }

  void RunAccNameTest(const base::FilePath::CharType* file_path) {
    base::FilePath test_path = GetTestFilePath("accessibility", "accname");
    {
      base::ScopedAllowBlockingForTesting allow_blocking;
      ASSERT_TRUE(base::PathExists(test_path)) << test_path.LossyDisplayName();
    }
    base::FilePath accname_file = test_path.Append(base::FilePath(file_path));
    RunTest(accname_file, "accessibility/accname");
  }
};

// Parameterize the tests so that each test-pass is run independently.
struct TestPassToString {
  std::string operator()(
      const ::testing::TestParamInfo<AXInspectFactory::Type>& i) const {
    return std::string(i.param);
  }
};

INSTANTIATE_TEST_SUITE_P(
    All,
    DumpAccessibilityNodeTest,
    ::testing::ValuesIn(DumpAccessibilityTestHelper::TreeTestPasses()),
    TestPassToString());

INSTANTIATE_TEST_SUITE_P(
    All,
    DumpAccessibilityAccNameTest,
    ::testing::ValuesIn(DumpAccessibilityTestHelper::TreeTestPasses()),
    TestPassToString());

// ARIA tests.
IN_PROC_BROWSER_TEST_P(DumpAccessibilityNodeTest, AccessibilityAriaScrollbar) {
  RunAriaTest(FILE_PATH_LITERAL("aria-scrollbar.html"));
}

// HTML tests.
IN_PROC_BROWSER_TEST_P(DumpAccessibilityNodeTest,
                       AccessibilityTableThColHeader) {
  RunHtmlTest(FILE_PATH_LITERAL("table-th-colheader.html"));
}

// AccName tests.
IN_PROC_BROWSER_TEST_P(DumpAccessibilityAccNameTest, DescComboboxFocusable) {
  RunAccNameTest(FILE_PATH_LITERAL("desc-combobox-focusable.html"));
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityAccNameTest,
                       DescFromContentOfDescribedbyElement) {
  RunAccNameTest(
      FILE_PATH_LITERAL("desc-from-content-of-describedby-element.html"));
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityAccNameTest,
                       DescImgAltDescribedbyHidden) {
  RunAccNameTest(FILE_PATH_LITERAL("desc-img-alt-describedby-hidden.html"));
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityAccNameTest, DescImgAltDescribedby) {
  RunAccNameTest(FILE_PATH_LITERAL("desc-img-alt-describedby.html"));
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityAccNameTest,
                       DescImgAltDescribedbyInvalid) {
  RunAccNameTest(FILE_PATH_LITERAL("desc-img-alt-describedby-invalid.html"));
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityAccNameTest,
                       DescImgAltDescribedbyNotDisplayed) {
  RunAccNameTest(
      FILE_PATH_LITERAL("desc-img-alt-describedby-not-displayed.html"));
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityAccNameTest,
                       DescImgAltDescribedbyPresentational) {
  RunAccNameTest(
      FILE_PATH_LITERAL("desc-img-alt-describedby-presentational.html"));
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityAccNameTest, DescImgDescribedby) {
  RunAccNameTest(FILE_PATH_LITERAL("desc-img-describedby.html"));
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityAccNameTest,
                       DescImgDescribedbyNotDisplayed) {
  RunAccNameTest(FILE_PATH_LITERAL("desc-img-describedby-not-displayed.html"));
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityAccNameTest,
                       DescImgDescribedbyPresentational) {
  RunAccNameTest(FILE_PATH_LITERAL("desc-img-describedby-presentational.html"));
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityAccNameTest, DescImgLabelAltTitle) {
  RunAccNameTest(FILE_PATH_LITERAL("desc-img-label-alt-title.html"));
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityAccNameTest,
                       DescImgOneValidDescribedby) {
  RunAccNameTest(FILE_PATH_LITERAL("desc-img-one-valid-describedby.html"));
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityAccNameTest,
                       DescInputTitleAndDescribedby) {
  RunAccNameTest(FILE_PATH_LITERAL("desc-input-title-and-describedby.html"));
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityAccNameTest,
                       DescLinkWithLabelAndTitle) {
  RunAccNameTest(FILE_PATH_LITERAL("desc-link-with-label-and-title.html"));
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityAccNameTest, NameButtonLabel) {
  RunAccNameTest(FILE_PATH_LITERAL("name-button-label.html"));
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityAccNameTest,
                       NameButtonLabelLabelledby) {
  RunAccNameTest(FILE_PATH_LITERAL("name-button-label-labelledby.html"));
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityAccNameTest, NameButtonLabelledby) {
  RunAccNameTest(FILE_PATH_LITERAL("name-button-labelledby.html"));
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityAccNameTest,
                       NameButtonRoleContentOnly) {
  RunAccNameTest(FILE_PATH_LITERAL("name-button-role-content-only.html"));
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityAccNameTest, NameButtonRoleTitleOnly) {
  RunAccNameTest(FILE_PATH_LITERAL("name-button-role-title-only.html"));
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityAccNameTest, NameButtonValue) {
  RunAccNameTest(FILE_PATH_LITERAL("name-button-value.html"));
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityAccNameTest,
                       NameCheckboxCssAfterInLabel) {
  RunAccNameTest(FILE_PATH_LITERAL("name-checkbox-css-after-in-label.html"));
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityAccNameTest,
                       NameCheckboxCssBeforeInLabel) {
  RunAccNameTest(FILE_PATH_LITERAL("name-checkbox-css-before-in-label.html"));
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityAccNameTest, NameCheckboxInputInLabel) {
  RunAccNameTest(FILE_PATH_LITERAL("name-checkbox-input-in-label.html"));
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityAccNameTest,
                       NameCheckboxInsideLabelWithGeneratedContent) {
  RunAccNameTest(FILE_PATH_LITERAL(
      "name-checkbox-inside-label-with-generated-content.html"));
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityAccNameTest,
                       NameCheckboxLabelEmbeddedCombobox) {
  RunAccNameTest(
      FILE_PATH_LITERAL("name-checkbox-label-embedded-combobox.html"));
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityAccNameTest,
                       NameCheckboxLabelEmbeddedListbox) {
  RunAccNameTest(
      FILE_PATH_LITERAL("name-checkbox-label-embedded-listbox.html"));
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityAccNameTest,
                       NameCheckboxLabelEmbeddedMenu) {
  RunAccNameTest(FILE_PATH_LITERAL("name-checkbox-label-embedded-menu.html"));
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityAccNameTest,
                       NameCheckboxLabelEmbeddedSelect) {
  RunAccNameTest(FILE_PATH_LITERAL("name-checkbox-label-embedded-select.html"));
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityAccNameTest,
                       NameCheckboxLabelEmbeddedSlider) {
  RunAccNameTest(FILE_PATH_LITERAL("name-checkbox-label-embedded-slider.html"));
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityAccNameTest,
                       NameCheckboxLabelEmbeddedSpinbutton) {
  RunAccNameTest(
      FILE_PATH_LITERAL("name-checkbox-label-embedded-spinbutton.html"));
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityAccNameTest,
                       NameCheckboxLabelEmbeddedTextbox) {
  RunAccNameTest(
      FILE_PATH_LITERAL("name-checkbox-label-embedded-textbox.html"));
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityAccNameTest, NameCheckboxLabel) {
  RunAccNameTest(FILE_PATH_LITERAL("name-checkbox-label.html"));
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityAccNameTest,
                       NameCheckboxLabelMultipleLabelAlternative) {
  RunAccNameTest(
      FILE_PATH_LITERAL("name-checkbox-label-multiple-label-alternative.html"));
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityAccNameTest,
                       NameCheckboxLabelMultipleLabel) {
  RunAccNameTest(FILE_PATH_LITERAL("name-checkbox-label-multiple-label.html"));
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityAccNameTest,
                       NameCheckboxLabelWithInput) {
  RunAccNameTest(FILE_PATH_LITERAL("name-checkbox-label-with-input.html"));
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityAccNameTest,
                       NameCheckboxSpinbuttonValuenowInLabel) {
  RunAccNameTest(
      FILE_PATH_LITERAL("name-checkbox-spinbutton-valuenow-in-label.html"));
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityAccNameTest, NameCheckboxTitle) {
  RunAccNameTest(FILE_PATH_LITERAL("name-checkbox-title.html"));
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityAccNameTest,
                       NameComboboxFocusableAlternative) {
  RunAccNameTest(FILE_PATH_LITERAL("name-combobox-focusable-alternative.html"));
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityAccNameTest, NameComboboxFocusable) {
  RunAccNameTest(FILE_PATH_LITERAL("name-combobox-focusable.html"));
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityAccNameTest, NameDivContentOnly) {
  RunAccNameTest(FILE_PATH_LITERAL("name-div-content-only.html"));
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityAccNameTest, NameDivLabel) {
  RunAccNameTest(FILE_PATH_LITERAL("name-div-label.html"));
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityAccNameTest, NameDivLabelLabelledby) {
  RunAccNameTest(FILE_PATH_LITERAL("name-div-label-labelledby.html"));
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityAccNameTest, NameDivLabelledby) {
  RunAccNameTest(FILE_PATH_LITERAL("name-div-labelledby.html"));
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityAccNameTest, NameDivMultipleSources) {
  RunAccNameTest(FILE_PATH_LITERAL("name-div-multiple-sources.html"));
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityAccNameTest, NameFileCssAfterInLabel) {
  RunAccNameTest(FILE_PATH_LITERAL("name-file-css-after-in-label.html"));
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityAccNameTest, NameFileCssBeforeInLabel) {
  RunAccNameTest(FILE_PATH_LITERAL("name-file-css-before-in-label.html"));
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityAccNameTest, NameFileInLabel) {
  RunAccNameTest(FILE_PATH_LITERAL("name-file-in-label.html"));
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityAccNameTest,
                       NameFileLabelEmbeddedCombobox) {
  RunAccNameTest(FILE_PATH_LITERAL("name-file-label-embedded-combobox.html"));
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityAccNameTest,
                       NameFileLabelEmbeddedMenu) {
  RunAccNameTest(FILE_PATH_LITERAL("name-file-label-embedded-menu.html"));
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityAccNameTest,
                       NameFileLabelEmbeddedSelect) {
  RunAccNameTest(FILE_PATH_LITERAL("name-file-label-embedded-select.html"));
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityAccNameTest,
                       NameFileLabelEmbeddedSlider) {
  RunAccNameTest(FILE_PATH_LITERAL("name-file-label-embedded-slider.html"));
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityAccNameTest,
                       NameFileLabelEmbeddedSpinbutton) {
  RunAccNameTest(FILE_PATH_LITERAL("name-file-label-embedded-spinbutton.html"));
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityAccNameTest, NameFileLabel) {
  RunAccNameTest(FILE_PATH_LITERAL("name-file-label.html"));
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityAccNameTest,
                       NameFileLabelInlineBlockElements) {
  RunAccNameTest(
      FILE_PATH_LITERAL("name-file-label-inline-block-elements.html"));
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityAccNameTest,
                       NameFileLabelInlineBlockStyles) {
  RunAccNameTest(FILE_PATH_LITERAL("name-file-label-inline-block-styles.html"));
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityAccNameTest,
                       NameFileLabelInlineHiddenElements) {
  RunAccNameTest(
      FILE_PATH_LITERAL("name-file-label-inline-hidden-elements.html"));
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityAccNameTest,
                       NameFileLabelOwnedCombobox) {
  RunAccNameTest(FILE_PATH_LITERAL("name-file-label-owned-combobox.html"));
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityAccNameTest,
                       NameFileLabelOwnedComboboxOwnedListbox) {
  RunAccNameTest(
      FILE_PATH_LITERAL("name-file-label-owned-combobox-owned-listbox.html"));
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityAccNameTest, NameFileLabelWithInput) {
  RunAccNameTest(FILE_PATH_LITERAL("name-file-label-with-input.html"));
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityAccNameTest,
                       NameFileSpinbuttonValuenowInLabel) {
  RunAccNameTest(
      FILE_PATH_LITERAL("name-file-spinbutton-valuenow-in-label.html"));
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityAccNameTest, NameFileTitle) {
  RunAccNameTest(FILE_PATH_LITERAL("name-file-title.html"));
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityAccNameTest, NameFromContent) {
  RunAccNameTest(FILE_PATH_LITERAL("name-from-content.html"));
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityAccNameTest, NameFromContentOfLabel) {
  RunAccNameTest(FILE_PATH_LITERAL("name-from-content-of-label.html"));
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityAccNameTest,
                       NameFromContentOfLabelledbyElement) {
  RunAccNameTest(
      FILE_PATH_LITERAL("name-from-content-of-labelledby-element.html"));
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityAccNameTest,
                       NameFromContentOfLabelledbyElementsOneOfWhichIsHidden) {
  RunAccNameTest(FILE_PATH_LITERAL(
      "name-from-content-of-labelledby-elements-one-of-which-is-hidden.html"));
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityAccNameTest,
                       NameHeadingComboboxFocusableAlternative) {
  RunAccNameTest(
      FILE_PATH_LITERAL("name-heading-combobox-focusable-alternative.html"));
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityAccNameTest, NameImageCssAfterInLabel) {
  RunAccNameTest(FILE_PATH_LITERAL("name-image-css-after-in-label.html"));
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityAccNameTest,
                       NameImageCssBeforeInLabel) {
  RunAccNameTest(FILE_PATH_LITERAL("name-image-css-before-in-label.html"));
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityAccNameTest, NameImageInputAlt) {
  RunAccNameTest(FILE_PATH_LITERAL("name-image-input-alt.html"));
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityAccNameTest,
                       NameImageInputInsideLabelWithGeneratedContent) {
  RunAccNameTest(FILE_PATH_LITERAL(
      "name-image-input-inside-label-with-generated-content.html"));
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityAccNameTest, NameImageInputLabel) {
  RunAccNameTest(FILE_PATH_LITERAL("name-image-input-label.html"));
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityAccNameTest,
                       NameImageInputLabelWithInput) {
  RunAccNameTest(FILE_PATH_LITERAL("name-image-input-label-with-input.html"));
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityAccNameTest,
                       NameImageSpinbuttonValuenowInLabel) {
  RunAccNameTest(
      FILE_PATH_LITERAL("name-image-spinbutton-valuenow-in-label.html"));
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityAccNameTest, NameImageTitle) {
  RunAccNameTest(FILE_PATH_LITERAL("name-image-title.html"));
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityAccNameTest, NameImgLabelAltTitle) {
  RunAccNameTest(FILE_PATH_LITERAL("name-img-label-alt-title.html"));
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityAccNameTest, NameImgLabel) {
  RunAccNameTest(FILE_PATH_LITERAL("name-img-label.html"));
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityAccNameTest, NameImgLabelledbyInputs) {
  RunAccNameTest(FILE_PATH_LITERAL("name-img-labelledby-inputs.html"));
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityAccNameTest,
                       NameImgLabelledbySelfAndInput) {
  RunAccNameTest(FILE_PATH_LITERAL("name-img-labelledby-self-and-input.html"));
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityAccNameTest,
                       NameImgLabelledbySelfAndTwoInputs) {
  RunAccNameTest(
      FILE_PATH_LITERAL("name-img-labelledby-self-and-two-inputs.html"));
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityAccNameTest, NameImgLabelledbySelf) {
  RunAccNameTest(FILE_PATH_LITERAL("name-img-labelledby-self.html"));
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityAccNameTest, NameImgMultipleSources) {
  RunAccNameTest(FILE_PATH_LITERAL("name-img-multiple-sources.html"));
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityAccNameTest,
                       NameImgMultipleSourcesSomeEmpty) {
  RunAccNameTest(
      FILE_PATH_LITERAL("name-img-multiple-sources-some-empty.html"));
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityAccNameTest,
                       NameImgWithLabelLabelsItself) {
  RunAccNameTest(FILE_PATH_LITERAL("name-img-with-label-labels-itself.html"));
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityAccNameTest, NameLinkContentOnly) {
  RunAccNameTest(FILE_PATH_LITERAL("name-link-content-only.html"));
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityAccNameTest, NameLinkEmptyWithTitle) {
  RunAccNameTest(FILE_PATH_LITERAL("name-link-empty-with-title.html"));
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityAccNameTest, NameLinkLabel) {
  RunAccNameTest(FILE_PATH_LITERAL("name-link-label.html"));
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityAccNameTest, NameLinkLabelledby) {
  RunAccNameTest(FILE_PATH_LITERAL("name-link-labelledby.html"));
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityAccNameTest,
                       NameLinkLabelledbySelfAndParagraph) {
  RunAccNameTest(
      FILE_PATH_LITERAL("name-link-labelledby-self-and-paragraph.html"));
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityAccNameTest, NameLinkLabelTitle) {
  RunAccNameTest(FILE_PATH_LITERAL("name-link-label-title.html"));
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityAccNameTest, NameLinkMixedContent) {
  RunAccNameTest(FILE_PATH_LITERAL("name-link-mixed-content.html"));
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityAccNameTest, NameLinkMultipleSources) {
  RunAccNameTest(FILE_PATH_LITERAL("name-link-multiple-sources.html"));
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityAccNameTest,
                       NamePasswordCssAfterInLabel) {
  RunAccNameTest(FILE_PATH_LITERAL("name-password-css-after-in-label.html"));
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityAccNameTest,
                       NamePasswordCssBeforeInLabel) {
  RunAccNameTest(FILE_PATH_LITERAL("name-password-css-before-in-label.html"));
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityAccNameTest, NamePasswordInputInLabel) {
  RunAccNameTest(FILE_PATH_LITERAL("name-password-input-in-label.html"));
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityAccNameTest,
                       NamePasswordInsideLabelWithGeneratedContent) {
  RunAccNameTest(FILE_PATH_LITERAL(
      "name-password-inside-label-with-generated-content.html"));
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityAccNameTest,
                       NamePasswordLabelEmbeddedCombobox) {
  RunAccNameTest(
      FILE_PATH_LITERAL("name-password-label-embedded-combobox.html"));
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityAccNameTest,
                       NamePasswordLabelEmbeddedMenu) {
  RunAccNameTest(FILE_PATH_LITERAL("name-password-label-embedded-menu.html"));
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityAccNameTest,
                       NamePasswordLabelEmbeddedSelect) {
  RunAccNameTest(FILE_PATH_LITERAL("name-password-label-embedded-select.html"));
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityAccNameTest,
                       NamePasswordLabelEmbeddedSlider) {
  RunAccNameTest(FILE_PATH_LITERAL("name-password-label-embedded-slider.html"));
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityAccNameTest,
                       NamePasswordLabelEmbeddedSpinbutton) {
  RunAccNameTest(
      FILE_PATH_LITERAL("name-password-label-embedded-spinbutton.html"));
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityAccNameTest, NamePasswordLabel) {
  RunAccNameTest(FILE_PATH_LITERAL("name-password-label.html"));
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityAccNameTest,
                       NamePasswordLabelWithInput) {
  RunAccNameTest(FILE_PATH_LITERAL("name-password-label-with-input.html"));
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityAccNameTest, NamePasswordTitle) {
  RunAccNameTest(FILE_PATH_LITERAL("name-password-title.html"));
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityAccNameTest, NameRadioCssAfterInLabel) {
  RunAccNameTest(FILE_PATH_LITERAL("name-radio-css-after-in-label.html"));
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityAccNameTest,
                       NameRadioCssBeforeInLabel) {
  RunAccNameTest(FILE_PATH_LITERAL("name-radio-css-before-in-label.html"));
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityAccNameTest, NameRadioInputInLabel) {
  RunAccNameTest(FILE_PATH_LITERAL("name-radio-input-in-label.html"));
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityAccNameTest,
                       NameRadioInsideLabelWithGeneratedContent) {
  RunAccNameTest(
      FILE_PATH_LITERAL("name-radio-inside-label-with-generated-content.html"));
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityAccNameTest,
                       NameRadioLabelEmbeddedCombobox) {
  RunAccNameTest(FILE_PATH_LITERAL("name-radio-label-embedded-combobox.html"));
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityAccNameTest,
                       NameRadioLabelEmbeddedMenu) {
  RunAccNameTest(FILE_PATH_LITERAL("name-radio-label-embedded-menu.html"));
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityAccNameTest,
                       NameRadioLabelEmbeddedSelect) {
  RunAccNameTest(FILE_PATH_LITERAL("name-radio-label-embedded-select.html"));
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityAccNameTest,
                       NameRadioLabelEmbeddedSlider) {
  RunAccNameTest(FILE_PATH_LITERAL("name-radio-label-embedded-slider.html"));
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityAccNameTest,
                       NameRadioLabelEmbeddedSpinbutton) {
  RunAccNameTest(
      FILE_PATH_LITERAL("name-radio-label-embedded-spinbutton.html"));
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityAccNameTest, NameRadioLabel) {
  RunAccNameTest(FILE_PATH_LITERAL("name-radio-label.html"));
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityAccNameTest, NameRadioLabelWithInput) {
  RunAccNameTest(FILE_PATH_LITERAL("name-radio-label-with-input.html"));
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityAccNameTest,
                       NameRadioSpinbuttonValuenowInLabel) {
  RunAccNameTest(
      FILE_PATH_LITERAL("name-radio-spinbutton-valuenow-in-label.html"));
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityAccNameTest, NameRadioTitle) {
  RunAccNameTest(FILE_PATH_LITERAL("name-radio-title.html"));
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityAccNameTest, NameResetButton) {
  RunAccNameTest(FILE_PATH_LITERAL("name-reset-button.html"));
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityAccNameTest, NameTextCssAfterInLabel) {
  RunAccNameTest(FILE_PATH_LITERAL("name-text-css-after-in-label.html"));
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityAccNameTest, NameTextCssBeforeInLabel) {
  RunAccNameTest(FILE_PATH_LITERAL("name-text-css-before-in-label.html"));
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityAccNameTest, NameTextInputInLabel) {
  RunAccNameTest(FILE_PATH_LITERAL("name-text-input-in-label.html"));
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityAccNameTest,
                       NameTextInsideLabelWithGeneratedContent) {
  RunAccNameTest(
      FILE_PATH_LITERAL("name-text-inside-label-with-generated-content.html"));
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityAccNameTest,
                       NameTextLabelEmbeddedCombobox) {
  RunAccNameTest(FILE_PATH_LITERAL("name-text-label-embedded-combobox.html"));
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityAccNameTest,
                       NameTextLabelEmbeddedMenu) {
  RunAccNameTest(FILE_PATH_LITERAL("name-text-label-embedded-menu.html"));
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityAccNameTest,
                       NameTextLabelEmbeddedSelect) {
  RunAccNameTest(FILE_PATH_LITERAL("name-text-label-embedded-select.html"));
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityAccNameTest,
                       NameTextLabelEmbeddedSlider) {
  RunAccNameTest(FILE_PATH_LITERAL("name-text-label-embedded-slider.html"));
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityAccNameTest,
                       NameTextLabelEmbeddedSpinbutton) {
  RunAccNameTest(FILE_PATH_LITERAL("name-text-label-embedded-spinbutton.html"));
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityAccNameTest, NameTextLabel) {
  RunAccNameTest(FILE_PATH_LITERAL("name-text-label.html"));
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityAccNameTest,
                       NameTextLabelledbyParagraphs) {
  RunAccNameTest(FILE_PATH_LITERAL("name-text-labelledby-paragraphs.html"));
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityAccNameTest,
                       NameTextLabelledbySelfAndDiv) {
  RunAccNameTest(FILE_PATH_LITERAL("name-text-labelledby-self-and-div.html"));
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityAccNameTest, NameTextLabelWithInput) {
  RunAccNameTest(FILE_PATH_LITERAL("name-text-label-with-input.html"));
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityAccNameTest, NameTextSelectInLabel) {
  RunAccNameTest(FILE_PATH_LITERAL("name-text-select-in-label.html"));
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityAccNameTest,
                       NameTextSpinbuttonValuenowInLabel) {
  RunAccNameTest(
      FILE_PATH_LITERAL("name-text-spinbutton-valuenow-in-label.html"));
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityAccNameTest,
                       NameTextSpinbuttonValuetextInLabel) {
  RunAccNameTest(
      FILE_PATH_LITERAL("name-text-spinbutton-valuetext-in-label.html"));
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityAccNameTest, NameTextTitle) {
  RunAccNameTest(FILE_PATH_LITERAL("name-text-title.html"));
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityAccNameTest, NameTextTitleValue) {
  RunAccNameTest(FILE_PATH_LITERAL("name-text-title-value.html"));
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityAccNameTest, NameTextWithLabel) {
  RunAccNameTest(FILE_PATH_LITERAL("name-text-with-label.html"));
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityAccNameTest,
                       NameTextWithValueLabelsImg) {
  RunAccNameTest(FILE_PATH_LITERAL("name-text-with-value-labels-img.html"));
}

IN_PROC_BROWSER_TEST_P(DumpAccessibilityAccNameTest,
                       NameTextWithValueLabelsImgWithLabel) {
  RunAccNameTest(
      FILE_PATH_LITERAL("name-text-with-value-labels-img-with-label.html"));
}

}  // namespace content
