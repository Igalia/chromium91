// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_PUBLIC_TEST_DUMP_ACCESSIBILITY_TEST_HELPER_H_
#define CONTENT_PUBLIC_TEST_DUMP_ACCESSIBILITY_TEST_HELPER_H_

#include "base/files/file_path.h"
#include "base/gtest_prod_util.h"
#include "base/optional.h"
#include "content/public/browser/ax_inspect_factory.h"

namespace base {
class CommandLine;
class FilePath;
}

namespace ui {
class AXInspectScenario;
struct AXPropertyFilter;
}  // namespace ui

namespace content {

// A helper class for writing accessibility tree dump tests.
class DumpAccessibilityTestHelper {
 public:
  explicit DumpAccessibilityTestHelper(AXInspectFactory::Type type);
  explicit DumpAccessibilityTestHelper(const char* expectation_type);
  ~DumpAccessibilityTestHelper() = default;

  // Returns a path to an expectation file for the current platform. If no
  // suitable expectation file can be found, logs an error message and returns
  // an empty path.
  base::FilePath GetExpectationFilePath(
      const base::FilePath& test_file_path,
      const base::FilePath::StringType& expectations_qualifier =
          FILE_PATH_LITERAL(""));

  // Sets up a command line for the test.
  void SetUpCommandLine(base::CommandLine*) const;

  // Parses a given testing scenario. Prepends default property filters if any
  // so the test file filters will take precedence over default filters in case
  // of conflict.
  ui::AXInspectScenario ParseScenario(
      const std::vector<std::string>& lines,
      const std::vector<ui::AXPropertyFilter>& default_filters = {});

  // Returns a platform-dependent list of inspect types used in dump tree
  // testing.
  static std::vector<AXInspectFactory::Type> TreeTestPasses();

  // Returns a platform-dependent list of inspect types used in dump events
  // testing.
  static std::vector<AXInspectFactory::Type> EventTestPasses();

  // Loads the given expectation file and returns the contents. An expectation
  // file may be empty, in which case an empty vector is returned.
  // Returns nullopt if the file contains a skip marker.
  static base::Optional<std::vector<std::string>> LoadExpectationFile(
      const base::FilePath& expected_file);

  // Compares the given actual dump against the given expectation and generates
  // a new expectation file if switches::kGenerateAccessibilityTestExpectations
  // has been set. Returns true if the result matches the expectation.
  static bool ValidateAgainstExpectation(
      const base::FilePath& test_file_path,
      const base::FilePath& expected_file,
      const std::vector<std::string>& actual_lines,
      const std::vector<std::string>& expected_lines);

 private:
  // Suffix of the expectation file corresponding to html file.
  // Overridden by each platform subclass.
  // Example:
  // HTML test:      test-file.html
  // Expected:       test-file-expected-mac.txt.
  //
  // In order to support multiple tests for the same html file, an
  // optional expectations_qualifier string may be specified. For
  // example, we could have both dump-tree and dump-node tests:
  // HTML test:      test-file.html
  // Expected:       test-file-node-expected-mac.txt
  // Expected:       test-file-tree-expected-mac.txt
  base::FilePath::StringType GetExpectedFileSuffix(
      const base::FilePath::StringType& expectations_qualifier =
          FILE_PATH_LITERAL("")) const;

  // Some Platforms expect different outputs depending on the version.
  // Most test outputs are identical but this allows a version specific
  // expected file to be used.
  base::FilePath::StringType GetVersionSpecificExpectedFileSuffix(
      const base::FilePath::StringType& expectations_qualifier =
          FILE_PATH_LITERAL("")) const;

  FRIEND_TEST_ALL_PREFIXES(DumpAccessibilityTestHelperTest, TestDiffLines);

  // Utility helper that does a comment-aware equality check.
  // Returns array of lines from expected file which are different.
  static std::vector<int> DiffLines(
      const std::vector<std::string>& expected_lines,
      const std::vector<std::string>& actual_lines);

  std::string expectation_type_;
};

}  // namespace content

#endif  // CONTENT_PUBLIC_TEST_DUMP_ACCESSIBILITY_TEST_HELPER_H_
