// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.content.browser.accessibility;

import static android.view.accessibility.AccessibilityNodeInfo.ACTION_CLICK;
import static android.view.accessibility.AccessibilityNodeInfo.ACTION_NEXT_HTML_ELEMENT;
import static android.view.accessibility.AccessibilityNodeInfo.AccessibilityAction.ACTION_LONG_CLICK;
import static android.view.accessibility.AccessibilityNodeInfo.AccessibilityAction.ACTION_SCROLL_BACKWARD;
import static android.view.accessibility.AccessibilityNodeInfo.AccessibilityAction.ACTION_SCROLL_DOWN;
import static android.view.accessibility.AccessibilityNodeInfo.AccessibilityAction.ACTION_SCROLL_FORWARD;
import static android.view.accessibility.AccessibilityNodeInfo.AccessibilityAction.ACTION_SCROLL_LEFT;
import static android.view.accessibility.AccessibilityNodeInfo.AccessibilityAction.ACTION_SCROLL_RIGHT;
import static android.view.accessibility.AccessibilityNodeInfo.AccessibilityAction.ACTION_SCROLL_UP;
import static android.view.accessibility.AccessibilityNodeInfo.AccessibilityAction.ACTION_SET_TEXT;
import static android.view.accessibility.AccessibilityNodeInfo.EXTRA_DATA_TEXT_CHARACTER_LOCATION_ARG_LENGTH;
import static android.view.accessibility.AccessibilityNodeInfo.EXTRA_DATA_TEXT_CHARACTER_LOCATION_ARG_START_INDEX;
import static android.view.accessibility.AccessibilityNodeInfo.EXTRA_DATA_TEXT_CHARACTER_LOCATION_KEY;

import static org.chromium.content.browser.accessibility.AccessibilityContentShellTestUtils.NODE_TIMEOUT_ERROR;
import static org.chromium.content.browser.accessibility.AccessibilityContentShellTestUtils.sClassNameMatcher;
import static org.chromium.content.browser.accessibility.AccessibilityContentShellTestUtils.sInputTypeMatcher;
import static org.chromium.content.browser.accessibility.AccessibilityContentShellTestUtils.sRangeInfoMatcher;
import static org.chromium.content.browser.accessibility.AccessibilityContentShellTestUtils.sTextMatcher;
import static org.chromium.content.browser.accessibility.AccessibilityContentShellTestUtils.sTextOrContentDescriptionMatcher;

import android.annotation.SuppressLint;
import android.annotation.TargetApi;
import android.graphics.RectF;
import android.os.Build;
import android.os.Bundle;
import android.text.InputType;
import android.text.Spannable;
import android.text.style.SuggestionSpan;
import android.view.accessibility.AccessibilityNodeInfo;

import androidx.test.filters.LargeTest;
import androidx.test.filters.SmallTest;

import org.hamcrest.Matchers;
import org.junit.After;
import org.junit.Assert;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;

import org.chromium.base.test.BaseJUnit4ClassRunner;
import org.chromium.base.test.util.Criteria;
import org.chromium.base.test.util.CriteriaHelper;
import org.chromium.base.test.util.FlakyTest;
import org.chromium.base.test.util.MinAndroidSdkLevel;
import org.chromium.base.test.util.UrlUtils;
import org.chromium.content_public.browser.test.util.TestThreadUtils;

import java.util.concurrent.ExecutionException;

/**
 * Tests for WebContentsAccessibility. Actually tests WebContentsAccessibilityImpl that
 * implements the interface.
 */
@RunWith(BaseJUnit4ClassRunner.class)
@MinAndroidSdkLevel(Build.VERSION_CODES.LOLLIPOP)
@TargetApi(Build.VERSION_CODES.LOLLIPOP)
@SuppressLint("VisibleForTests")
public class WebContentsAccessibilityTest {
    // Test output error messages
    private static final String COMBOBOX_ERROR = "expanded combobox announcement was incorrect.";
    private static final String DISABLED_COMBOBOX_ERROR =
            "disabled combobox child elements should not be clickable";
    private static final String LONG_CLICK_ERROR =
            "node should not have the ACTION_LONG_CLICK action as an available action";
    private static final String ACTION_SET_ERROR =
            "node should have the ACTION_SET_TEXT action as an available action";
    private static final String THRESHOLD_ERROR =
            "Too many TYPE_WINDOW_CONTENT_CHANGED events received in an atomic update.";
    private static final String THRESHOLD_LOW_EVENT_COUNT_ERROR =
            "Expected more TYPE_WINDOW_CONTENT_CHANGED events"
            + "in an atomic update, is throttling still necessary?";
    private static final String ARIA_INVALID_ERROR =
            "Error message for aria-invalid node has not been set correctly.";
    private static final String CONTENTEDITABLE_ERROR =
            "contenteditable node is not being identified and/or received incorrect class name";
    private static final String SPELLING_ERROR =
            "node should have a Spannable with spelling correction for given text.";
    private static final String INPUT_RANGE_VALUE_MISMATCH =
            "Value for <input type='range'> is incorrect, did you honor 'step' value?";
    private static final String INPUT_RANGE_EVENT_ERROR =
            "TYPE_VIEW_SCROLLED event not received before timeout.";

    // Constant values for unit tests
    private static final int UNSUPPRESSED_EXPECTED_COUNT = 25;

    private AccessibilityNodeInfo mNodeInfo;
    private AccessibilityContentShellTestData mTestData;

    @Rule
    public AccessibilityContentShellActivityTestRule mActivityTestRule =
            new AccessibilityContentShellActivityTestRule();

    /**
     * Helper methods for setup of a basic web contents accessibility unit test.
     *
     * These methods replace the usual setUp() method annotated with @Before because we wish to
     * load different data with each test, but the process is the same for all tests.
     *
     * Leaving a commented @Before annotation on each method as a reminder/context clue.
     */
    /* @Before */
    protected void setupTestWithHTML(String html) {
        mActivityTestRule.launchContentShellWithUrl(UrlUtils.encodeHtmlDataUri(html));
        mActivityTestRule.waitForActiveShellToBeDoneLoading();
        mActivityTestRule.setupTestFramework();
        mActivityTestRule.setAccessibilityDelegate();

        mTestData = AccessibilityContentShellTestData.getInstance();
    }

    /* @Before */
    protected void setupTestFromFile(String filepath) {
        mActivityTestRule.launchContentShellWithUrl(UrlUtils.getIsolatedTestFileUrl(filepath));
        mActivityTestRule.waitForActiveShellToBeDoneLoading();
        mActivityTestRule.setupTestFramework();
        mActivityTestRule.setAccessibilityDelegate();

        mTestData = AccessibilityContentShellTestData.getInstance();
    }

    /**
     * Helper method to tear down our tests so we can start the next test clean.
     */
    @After
    public void tearDown() {
        mTestData = null;
        mNodeInfo = null;
    }

    // Helper pass-through methods to make tests easier to read.
    private <T> int waitForNodeMatching(
            AccessibilityContentShellTestUtils.AccessibilityNodeInfoMatcher<T> matcher, T element) {
        return mActivityTestRule.waitForNodeMatching(matcher, element);
    }

    private boolean performActionOnUiThread(int viewId, int action, Bundle args)
            throws ExecutionException {
        return mActivityTestRule.performActionOnUiThread(viewId, action, args);
    }

    private void executeJS(String method) {
        mActivityTestRule.executeJS(method);
    }

    private void focusNode(int virtualViewId) throws Throwable {
        mActivityTestRule.focusNode(virtualViewId);
    }

    public AccessibilityNodeInfo createAccessibilityNodeInfo(int virtualViewId) {
        return mActivityTestRule.mNodeProvider.createAccessibilityNodeInfo(virtualViewId);
    }

    /**
     * Test <input type="range"> nodes and events for incrementing/decrementing value with actions.
     */
    @Test
    @SmallTest
    @MinAndroidSdkLevel(Build.VERSION_CODES.O)
    public void testAccessibilityNodeInfo_inputTypeRange() throws Throwable {
        // Create a basic input range, and find the associated |AccessibilityNodeInfo| object.
        setupTestWithHTML("<input type='range' min='0' max='40'>");

        // Find the input range and assert we have the correct node.
        int inputNodeVirtualViewId = waitForNodeMatching(sRangeInfoMatcher, "");
        mNodeInfo = createAccessibilityNodeInfo(inputNodeVirtualViewId);
        Assert.assertNotNull(NODE_TIMEOUT_ERROR, mNodeInfo);
        Assert.assertEquals(NODE_TIMEOUT_ERROR, 0, mNodeInfo.getRangeInfo().getMin(), 0.001);
        Assert.assertEquals(NODE_TIMEOUT_ERROR, 40, mNodeInfo.getRangeInfo().getMax(), 0.001);

        // Perform a series of slider increments and check results.
        for (int i = 1; i <= 10; i++) {
            // Increment our slider using action, and poll until we receive the scroll event.
            performActionOnUiThread(inputNodeVirtualViewId,
                    AccessibilityNodeInfo.ACTION_SCROLL_FORWARD, new Bundle());
            CriteriaHelper.pollUiThread(
                    () -> mTestData.hasReceivedEvent(), INPUT_RANGE_EVENT_ERROR);

            // Refresh our node info to get the latest RangeInfo child object.
            mNodeInfo = createAccessibilityNodeInfo(inputNodeVirtualViewId);

            // Confirm slider values.
            Assert.assertEquals(INPUT_RANGE_VALUE_MISMATCH, 20 + (2 * i),
                    mNodeInfo.getRangeInfo().getCurrent(), 0.001);

            // Reset polling value for next test
            mTestData.setReceivedEvent(false);
        }

        // Perform a series of slider decrements and check results.
        for (int i = 1; i <= 20; i++) {
            // Decrement our slider using action, and poll until we receive the scroll event.
            performActionOnUiThread(inputNodeVirtualViewId,
                    AccessibilityNodeInfo.ACTION_SCROLL_BACKWARD, new Bundle());
            CriteriaHelper.pollUiThread(
                    () -> mTestData.hasReceivedEvent(), INPUT_RANGE_EVENT_ERROR);

            // Refresh our node info to get the latest RangeInfo child object.
            mNodeInfo = createAccessibilityNodeInfo(inputNodeVirtualViewId);

            // Confirm slider values.
            Assert.assertEquals(INPUT_RANGE_VALUE_MISMATCH, 40 - (2 * i),
                    mNodeInfo.getRangeInfo().getCurrent(), 0.001);

            // Reset polling value for next test
            mTestData.setReceivedEvent(false);
        }
    }

    /**
     * Ensure we are honoring min/max/step values for <input type="range"> nodes.
     */
    @Test
    @SmallTest
    @MinAndroidSdkLevel(Build.VERSION_CODES.O)
    public void testAccessibilityNodeInfo_inputTypeRange_withStepValue() throws Throwable {
        // Create a basic input range, and find the associated |AccessibilityNodeInfo| object.
        setupTestWithHTML("<input type='range' min='0' max='144' step='12'>");

        // Find the input range and assert we have the correct node.
        int inputNodeVirtualViewId = waitForNodeMatching(sRangeInfoMatcher, "");
        mNodeInfo = createAccessibilityNodeInfo(inputNodeVirtualViewId);
        Assert.assertNotNull(NODE_TIMEOUT_ERROR, mNodeInfo);
        Assert.assertEquals(NODE_TIMEOUT_ERROR, 0, mNodeInfo.getRangeInfo().getMin(), 0.001);
        Assert.assertEquals(NODE_TIMEOUT_ERROR, 144, mNodeInfo.getRangeInfo().getMax(), 0.001);

        // Perform a series of slider increments and check results.
        int[] expectedVals = new int[] {84, 96, 108, 120, 132, 144};
        for (int expectedVal : expectedVals) {
            // Increment our slider using action, and poll until we receive the scroll event.
            performActionOnUiThread(inputNodeVirtualViewId,
                    AccessibilityNodeInfo.ACTION_SCROLL_FORWARD, new Bundle());
            CriteriaHelper.pollUiThread(
                    () -> mTestData.hasReceivedEvent(), INPUT_RANGE_EVENT_ERROR);

            // Refresh our node info to get the latest RangeInfo child object.
            mNodeInfo = createAccessibilityNodeInfo(inputNodeVirtualViewId);

            // Confirm slider values.
            Assert.assertEquals(INPUT_RANGE_VALUE_MISMATCH, expectedVal,
                    mNodeInfo.getRangeInfo().getCurrent(), 0.001);

            // Reset polling value for next test
            mTestData.setReceivedEvent(false);
        }

        // Perform a series of slider decrements and check results.
        expectedVals = new int[] {132, 120, 108, 96, 84, 72, 60, 48, 36, 24, 12, 0};
        for (int expectedVal : expectedVals) {
            // Decrement our slider using action, and poll until we receive the scroll event.
            performActionOnUiThread(inputNodeVirtualViewId,
                    AccessibilityNodeInfo.ACTION_SCROLL_BACKWARD, new Bundle());
            CriteriaHelper.pollUiThread(
                    () -> mTestData.hasReceivedEvent(), INPUT_RANGE_EVENT_ERROR);

            // Refresh our node info to get the latest RangeInfo child object.
            mNodeInfo = createAccessibilityNodeInfo(inputNodeVirtualViewId);

            // Confirm slider values.
            Assert.assertEquals(INPUT_RANGE_VALUE_MISMATCH, expectedVal,
                    mNodeInfo.getRangeInfo().getCurrent(), 0.001);

            // Reset polling value for next test
            mTestData.setReceivedEvent(false);
        }
    }

    /**
     * Test <input type="range"> nodes move by a minimum value with increment/decrement actions.
     */
    @Test
    @SmallTest
    @MinAndroidSdkLevel(Build.VERSION_CODES.O)
    public void testAccessibilityNodeInfo_inputTypeRange_withRequiredMin() throws Throwable {
        // Create a basic input range, and find the associated |AccessibilityNodeInfo| object.
        setupTestWithHTML("<input type='range' min='0' max='1000' step='1'>");

        // Find the input range and assert we have the correct node.
        int inputNodeVirtualViewId = waitForNodeMatching(sRangeInfoMatcher, "");
        mNodeInfo = createAccessibilityNodeInfo(inputNodeVirtualViewId);
        Assert.assertNotNull(NODE_TIMEOUT_ERROR, mNodeInfo);
        Assert.assertEquals(NODE_TIMEOUT_ERROR, 0, mNodeInfo.getRangeInfo().getMin(), 0.001);
        Assert.assertEquals(NODE_TIMEOUT_ERROR, 1000, mNodeInfo.getRangeInfo().getMax(), 0.001);

        // Perform a series of slider increments and check results.
        for (int i = 1; i <= 10; i++) {
            // Increment our slider using action, and poll until we receive the scroll event.
            performActionOnUiThread(inputNodeVirtualViewId,
                    AccessibilityNodeInfo.ACTION_SCROLL_FORWARD, new Bundle());
            CriteriaHelper.pollUiThread(
                    () -> mTestData.hasReceivedEvent(), INPUT_RANGE_EVENT_ERROR);

            // Refresh our node info to get the latest RangeInfo child object.
            mNodeInfo = createAccessibilityNodeInfo(inputNodeVirtualViewId);

            // Confirm slider values.
            Assert.assertEquals(INPUT_RANGE_VALUE_MISMATCH, 500 + (10 * i),
                    mNodeInfo.getRangeInfo().getCurrent(), 0.001);

            // Reset polling value for next test
            mTestData.setReceivedEvent(false);
        }

        // Perform a series of slider decrements and check results.
        for (int i = 1; i <= 20; i++) {
            // Decrement our slider using action, and poll until we receive the scroll event.
            performActionOnUiThread(inputNodeVirtualViewId,
                    AccessibilityNodeInfo.ACTION_SCROLL_BACKWARD, new Bundle());
            CriteriaHelper.pollUiThread(
                    () -> mTestData.hasReceivedEvent(), INPUT_RANGE_EVENT_ERROR);

            // Refresh our node info to get the latest RangeInfo child object.
            mNodeInfo = createAccessibilityNodeInfo(inputNodeVirtualViewId);

            // Confirm slider values.
            Assert.assertEquals(INPUT_RANGE_VALUE_MISMATCH, 600 - (10 * i),
                    mNodeInfo.getRangeInfo().getCurrent(), 0.001);

            // Reset polling value for next test
            mTestData.setReceivedEvent(false);
        }
    }

    /**
     * Ensure we throttle TYPE_WINDOW_CONTENT_CHANGED events for large tree updates.
     */
    @Test
    @FlakyTest(message = "https://crbug.com/1161533")
    @SmallTest
    public void testMaxContentChangedEventsFired_default() throws Throwable {
        // Build a simple web page with complex visibility change.
        setupTestFromFile("content/test/data/android/type_window_content_changed_events.html");

        // Determine the current max events to fire
        int maxEvents = mActivityTestRule.mWcax.getMaxContentChangedEventsToFireForTesting();

        // Run JS code to expand comboboxes
        executeJS("expandComboboxes()");

        // Signal end of test
        mActivityTestRule.sendEndOfTestSignal();

        // Verify number of events processed
        int eventCount = mTestData.getTypeWindowContentChangedCount();
        Assert.assertTrue(thresholdError(eventCount, maxEvents), eventCount <= maxEvents);
    }

    /**
     * Ensure we need to throttle TYPE_WINDOW_CONTENT_CHANGED events for some large tree updates.
     */
    @Test
    @FlakyTest(message = "https://crbug.com/1161533")
    @SmallTest
    public void testMaxContentChangedEventsFired_largeLimit() throws Throwable {
        // Build a simple web page with complex visibility change.
        setupTestFromFile("content/test/data/android/type_window_content_changed_events.html");

        // "Disable" event suppression by setting an arbitrarily high max events value.
        mActivityTestRule.mWcax.setMaxContentChangedEventsToFireForTesting(Integer.MAX_VALUE);

        // Run JS code to expand comboboxes
        executeJS("expandComboboxes()");

        // Signal end of test
        mActivityTestRule.sendEndOfTestSignal();

        // Verify number of events processed
        int eventCount = mTestData.getTypeWindowContentChangedCount();
        Assert.assertTrue(lowThresholdError(eventCount), eventCount > UNSUPPRESSED_EXPECTED_COUNT);
    }

    /**
     * Ensure we send an announcement on combobox expansion.
     */
    @Test
    @SmallTest
    public void testEventText_Combobox() throws Throwable {
        // Build a simple web page with a combobox, and focus the input field.
        setupTestFromFile("content/test/data/android/input/input_combobox.html");

        // Find a node in the accessibility tree of the correct class.
        int comboBoxVirtualViewId =
                waitForNodeMatching(sClassNameMatcher, "android.widget.EditText");
        mNodeInfo = createAccessibilityNodeInfo(comboBoxVirtualViewId);
        Assert.assertNotNull(NODE_TIMEOUT_ERROR, mNodeInfo);

        focusNode(comboBoxVirtualViewId);

        // Run JS code to expand the combobox
        executeJS("expandCombobox()");

        // Signal end of test
        mActivityTestRule.sendEndOfTestSignal();

        // We should have received a TYPE_ANNOUNCEMENT event, check announcement text.
        Assert.assertEquals(COMBOBOX_ERROR, "expanded, 3 autocomplete options available.",
                mTestData.getAnnouncementText());
    }

    /**
     * Ensure we send an announcement on combobox expansion that opens a dialog.
     */
    @Test
    @SmallTest
    public void testEventText_Combobox_dialog() throws Throwable {
        // Build a simple web page with a combobox, and focus the input field.
        setupTestFromFile("content/test/data/android/input/input_combobox_dialog.html");

        // Find a node in the accessibility tree of the correct class.
        int comboBoxVirtualViewId =
                waitForNodeMatching(sClassNameMatcher, "android.widget.EditText");
        mNodeInfo = createAccessibilityNodeInfo(comboBoxVirtualViewId);
        Assert.assertNotNull(NODE_TIMEOUT_ERROR, mNodeInfo);

        focusNode(comboBoxVirtualViewId);

        // Run JS code to expand the combobox
        executeJS("expandCombobox()");

        // Signal end of test
        mActivityTestRule.sendEndOfTestSignal();

        // We should have received a TYPE_ANNOUNCEMENT event, check announcement text.
        Assert.assertEquals(
                COMBOBOX_ERROR, "expanded, dialog opened.", mTestData.getAnnouncementText());
    }

    /**
     * Ensure we send an announcement on combobox expansion with aria-1.0 spec.
     */
    @Test
    @SmallTest
    public void testEventText_Combobox_ariaOne() throws Throwable {
        // Build a simple web page with a combobox, and focus the input field.
        setupTestFromFile("content/test/data/android/input/input_combobox_aria1.0.html");

        // Find a node in the accessibility tree of the correct class.
        int comboBoxVirtualViewId =
                waitForNodeMatching(sClassNameMatcher, "android.widget.EditText");
        mNodeInfo = createAccessibilityNodeInfo(comboBoxVirtualViewId);
        Assert.assertNotNull(NODE_TIMEOUT_ERROR, mNodeInfo);

        focusNode(comboBoxVirtualViewId);

        // Run JS code to expand the combobox
        executeJS("expandCombobox()");

        // Signal end of test
        mActivityTestRule.sendEndOfTestSignal();

        // We should have received a TYPE_ANNOUNCEMENT event, check announcement text.
        Assert.assertEquals(COMBOBOX_ERROR, "expanded, 3 autocomplete options available.",
                mTestData.getAnnouncementText());
    }

    /**
     * Ensure that disabled comboboxes and children are not shadow clickable.
     */
    @Test
    @SmallTest
    public void testEvent_Combobox_disabled() throws Throwable {
        // Build a simple web page with a disabled combobox.
        setupTestWithHTML("<select disabled>\n"
                + "  <option>Volvo</option>\n"
                + "  <option>Saab</option>\n"
                + "  <option>Mercedes</option>\n"
                + "</select>");

        // Find the disabled option node and set a delegate to track focus.
        int disabledNodeId = waitForNodeMatching(sTextMatcher, "Volvo");
        mNodeInfo = createAccessibilityNodeInfo(disabledNodeId);
        Assert.assertNotNull(NODE_TIMEOUT_ERROR, mNodeInfo);

        focusNode(disabledNodeId);
        mTestData.setReceivedAccessibilityFocusEvent(false);

        // Perform a click on the node.
        performActionOnUiThread(disabledNodeId, ACTION_CLICK, null);

        // Signal end of test
        mActivityTestRule.sendEndOfTestSignal();

        // Check we did not receive any events.
        Assert.assertFalse(DISABLED_COMBOBOX_ERROR, mTestData.hasReceivedAccessibilityFocusEvent());
    }

    /**
     * Ensure traverse events and selection events are properly indexed when navigating an edit
     * field by character with selection mode off
     */
    @Test
    @SmallTest
    public void testEventIndices_SelectionOFF_CharacterGranularity() throws Throwable {
        // Build a simple web page with an input and the text "Testing"
        setupTestWithHTML("<input id=\"fn\" type=\"text\" value=\"Testing\">");

        // Find a node in the accessibility tree with input type TYPE_CLASS_TEXT.
        int editTextVirtualViewId =
                waitForNodeMatching(sInputTypeMatcher, InputType.TYPE_CLASS_TEXT);
        mNodeInfo = createAccessibilityNodeInfo(editTextVirtualViewId);
        Assert.assertNotEquals(mNodeInfo, null);

        focusNode(editTextVirtualViewId);

        // Set granularity to CHARACTER, with selection FALSE
        Bundle args = new Bundle();
        args.putInt(AccessibilityNodeInfo.ACTION_ARGUMENT_MOVEMENT_GRANULARITY_INT,
                AccessibilityNodeInfo.MOVEMENT_GRANULARITY_CHARACTER);
        args.putBoolean(AccessibilityNodeInfo.ACTION_ARGUMENT_EXTEND_SELECTION_BOOLEAN, false);

        // Simulate swiping left (backward)
        for (int i = 7; i > 0; i--) {
            performActionOnUiThread(editTextVirtualViewId,
                    AccessibilityNodeInfo.ACTION_PREVIOUS_AT_MOVEMENT_GRANULARITY, args);

            Assert.assertEquals(i - 1, mTestData.getTraverseFromIndex());
            Assert.assertEquals(i, mTestData.getTraverseToIndex());
            Assert.assertEquals(i - 1, mTestData.getSelectionFromIndex());
            Assert.assertEquals(i - 1, mTestData.getSelectionToIndex());
        }

        // Simulate swiping right (forward)
        for (int i = 0; i < 7; i++) {
            performActionOnUiThread(editTextVirtualViewId,
                    AccessibilityNodeInfo.ACTION_NEXT_AT_MOVEMENT_GRANULARITY, args);

            Assert.assertEquals(i, mTestData.getTraverseFromIndex());
            Assert.assertEquals(i + 1, mTestData.getTraverseToIndex());
            Assert.assertEquals(i + 1, mTestData.getSelectionFromIndex());
            Assert.assertEquals(i + 1, mTestData.getSelectionToIndex());
        }
    }

    /**
     * Ensure traverse events and selection events are properly indexed when navigating an edit
     * field by character with selection mode on
     */
    @Test
    @LargeTest
    public void testEventIndices_SelectionON_CharacterGranularity() throws Throwable {
        // Build a simple web page with an input and the text "Testing"
        setupTestWithHTML("<input id=\"fn\" type=\"text\" value=\"Testing\">");

        // Find a node in the accessibility tree with input type TYPE_CLASS_TEXT.
        int editTextVirtualViewId =
                waitForNodeMatching(sInputTypeMatcher, InputType.TYPE_CLASS_TEXT);
        mNodeInfo = createAccessibilityNodeInfo(editTextVirtualViewId);
        Assert.assertNotEquals(mNodeInfo, null);

        focusNode(editTextVirtualViewId);

        // Set granularity to CHARACTER, with selection TRUE
        Bundle args = new Bundle();
        args.putInt(AccessibilityNodeInfo.ACTION_ARGUMENT_MOVEMENT_GRANULARITY_INT,
                AccessibilityNodeInfo.MOVEMENT_GRANULARITY_CHARACTER);
        args.putBoolean(AccessibilityNodeInfo.ACTION_ARGUMENT_EXTEND_SELECTION_BOOLEAN, true);

        // Simulate swiping left (backward) (adds to selections)
        for (int i = 7; i > 0; i--) {
            performActionOnUiThread(editTextVirtualViewId,
                    AccessibilityNodeInfo.ACTION_PREVIOUS_AT_MOVEMENT_GRANULARITY, args);

            Assert.assertEquals(i - 1, mTestData.getTraverseFromIndex());
            Assert.assertEquals(i, mTestData.getTraverseToIndex());
            Assert.assertEquals(7, mTestData.getSelectionFromIndex());
            Assert.assertEquals(i - 1, mTestData.getSelectionToIndex());
        }

        // Simulate swiping right (forward) (removes from selection)
        for (int i = 0; i < 7; i++) {
            performActionOnUiThread(editTextVirtualViewId,
                    AccessibilityNodeInfo.ACTION_NEXT_AT_MOVEMENT_GRANULARITY, args);

            Assert.assertEquals(i, mTestData.getTraverseFromIndex());
            Assert.assertEquals(i + 1, mTestData.getTraverseToIndex());
            Assert.assertEquals(7, mTestData.getSelectionFromIndex());
            Assert.assertEquals(i + 1, mTestData.getSelectionToIndex());
        }

        // Turn selection mode off and traverse to beginning so we can select forwards
        args.putBoolean(AccessibilityNodeInfo.ACTION_ARGUMENT_EXTEND_SELECTION_BOOLEAN, false);
        for (int i = 7; i > 0; i--) {
            performActionOnUiThread(editTextVirtualViewId,
                    AccessibilityNodeInfo.ACTION_PREVIOUS_AT_MOVEMENT_GRANULARITY, args);
        }

        // Turn selection mode on
        args.putBoolean(AccessibilityNodeInfo.ACTION_ARGUMENT_EXTEND_SELECTION_BOOLEAN, true);

        // Simulate swiping right (forward) (adds to selection)
        for (int i = 0; i < 7; i++) {
            performActionOnUiThread(editTextVirtualViewId,
                    AccessibilityNodeInfo.ACTION_NEXT_AT_MOVEMENT_GRANULARITY, args);

            Assert.assertEquals(i, mTestData.getTraverseFromIndex());
            Assert.assertEquals(i + 1, mTestData.getTraverseToIndex());
            Assert.assertEquals(0, mTestData.getSelectionFromIndex());
            Assert.assertEquals(i + 1, mTestData.getSelectionToIndex());
        }

        // Simulate swiping left (backward) (removes from selections)
        for (int i = 7; i > 0; i--) {
            performActionOnUiThread(editTextVirtualViewId,
                    AccessibilityNodeInfo.ACTION_PREVIOUS_AT_MOVEMENT_GRANULARITY, args);

            Assert.assertEquals(i - 1, mTestData.getTraverseFromIndex());
            Assert.assertEquals(i, mTestData.getTraverseToIndex());
            Assert.assertEquals(0, mTestData.getSelectionFromIndex());
            Assert.assertEquals(i - 1, mTestData.getSelectionToIndex());
        }
    }

    /**
     * Ensure traverse events and selection events are properly indexed when navigating an edit
     * field by word with selection mode off
     */
    @Test
    @SmallTest
    public void testEventIndices_SelectionOFF_WordGranularity() throws Throwable {
        // Build a simple web page with an input and the text "Testing this output is correct"
        setupTestWithHTML(
                "<input id=\"fn\" type=\"text\" value=\"Testing this output is correct\">");

        // Find a node in the accessibility tree with input type TYPE_CLASS_TEXT.
        int editTextVirtualViewId =
                waitForNodeMatching(sInputTypeMatcher, InputType.TYPE_CLASS_TEXT);
        mNodeInfo = createAccessibilityNodeInfo(editTextVirtualViewId);
        Assert.assertNotEquals(mNodeInfo, null);

        focusNode(editTextVirtualViewId);

        // Set granularity to WORD, with selection FALSE
        Bundle args = new Bundle();
        args.putInt(AccessibilityNodeInfo.ACTION_ARGUMENT_MOVEMENT_GRANULARITY_INT,
                AccessibilityNodeInfo.MOVEMENT_GRANULARITY_WORD);
        args.putBoolean(AccessibilityNodeInfo.ACTION_ARGUMENT_EXTEND_SELECTION_BOOLEAN, false);

        int[] wordStarts = new int[] {0, 8, 13, 20, 23};
        int[] wordEnds = new int[] {7, 12, 19, 22, 30};

        // Simulate swiping left (backward) through all 5 words, check indices along the way
        for (int i = 4; i >= 0; --i) {
            performActionOnUiThread(editTextVirtualViewId,
                    AccessibilityNodeInfo.ACTION_PREVIOUS_AT_MOVEMENT_GRANULARITY, args);

            Assert.assertEquals(wordStarts[i], mTestData.getTraverseFromIndex());
            Assert.assertEquals(wordEnds[i], mTestData.getTraverseToIndex());
            Assert.assertEquals(wordStarts[i], mTestData.getSelectionFromIndex());
            Assert.assertEquals(wordStarts[i], mTestData.getSelectionToIndex());
        }

        // Simulate swiping right (forward) through all 5 words, check indices along the way
        for (int i = 0; i < 5; ++i) {
            performActionOnUiThread(editTextVirtualViewId,
                    AccessibilityNodeInfo.ACTION_NEXT_AT_MOVEMENT_GRANULARITY, args);

            Assert.assertEquals(wordStarts[i], mTestData.getTraverseFromIndex());
            Assert.assertEquals(wordEnds[i], mTestData.getTraverseToIndex());
            Assert.assertEquals(wordEnds[i], mTestData.getSelectionFromIndex());
            Assert.assertEquals(wordEnds[i], mTestData.getSelectionToIndex());
        }
    }

    /**
     * Ensure traverse events and selection events are properly indexed when navigating an edit
     * field by word with selection mode on
     */
    @Test
    @LargeTest
    public void testEventIndices_SelectionON_WordGranularity() throws Throwable {
        setupTestWithHTML(
                "<input id=\"fn\" type=\"text\" value=\"Testing this output is correct\">");

        // Find a node in the accessibility tree with input type TYPE_CLASS_TEXT.
        int editTextVirtualViewId =
                waitForNodeMatching(sInputTypeMatcher, InputType.TYPE_CLASS_TEXT);
        mNodeInfo = createAccessibilityNodeInfo(editTextVirtualViewId);
        Assert.assertNotEquals(mNodeInfo, null);

        focusNode(editTextVirtualViewId);

        // Set granularity to WORD, with selection TRUE
        Bundle args = new Bundle();
        args.putInt(AccessibilityNodeInfo.ACTION_ARGUMENT_MOVEMENT_GRANULARITY_INT,
                AccessibilityNodeInfo.MOVEMENT_GRANULARITY_WORD);
        args.putBoolean(AccessibilityNodeInfo.ACTION_ARGUMENT_EXTEND_SELECTION_BOOLEAN, true);

        int[] wordStarts = new int[] {0, 8, 13, 20, 23};
        int[] wordEnds = new int[] {7, 12, 19, 22, 30};

        // Simulate swiping left (backward, adds to selection) through all 5 words, check indices
        for (int i = 4; i >= 0; --i) {
            performActionOnUiThread(editTextVirtualViewId,
                    AccessibilityNodeInfo.ACTION_PREVIOUS_AT_MOVEMENT_GRANULARITY, args);

            Assert.assertEquals(wordStarts[i], mTestData.getTraverseFromIndex());
            Assert.assertEquals(wordEnds[i], mTestData.getTraverseToIndex());
            Assert.assertEquals(30, mTestData.getSelectionFromIndex());
            Assert.assertEquals(wordStarts[i], mTestData.getSelectionToIndex());
        }

        // Simulate swiping right (forward, removes selection) through all 5 words, check indices
        for (int i = 0; i < 5; ++i) {
            performActionOnUiThread(editTextVirtualViewId,
                    AccessibilityNodeInfo.ACTION_NEXT_AT_MOVEMENT_GRANULARITY, args);

            Assert.assertEquals(wordStarts[i], mTestData.getTraverseFromIndex());
            Assert.assertEquals(wordEnds[i], mTestData.getTraverseToIndex());
            Assert.assertEquals(30, mTestData.getSelectionFromIndex());
            Assert.assertEquals(wordEnds[i], mTestData.getSelectionToIndex());
        }

        // Turn selection mode off and traverse to beginning so we can select forwards
        args.putBoolean(AccessibilityNodeInfo.ACTION_ARGUMENT_EXTEND_SELECTION_BOOLEAN, false);
        for (int i = 4; i >= 0; i--) {
            performActionOnUiThread(editTextVirtualViewId,
                    AccessibilityNodeInfo.ACTION_PREVIOUS_AT_MOVEMENT_GRANULARITY, args);
        }

        // Turn selection mode on
        args.putBoolean(AccessibilityNodeInfo.ACTION_ARGUMENT_EXTEND_SELECTION_BOOLEAN, true);

        // Simulate swiping right (forward) (adds to selection)
        for (int i = 0; i < 5; ++i) {
            performActionOnUiThread(editTextVirtualViewId,
                    AccessibilityNodeInfo.ACTION_NEXT_AT_MOVEMENT_GRANULARITY, args);

            Assert.assertEquals(wordStarts[i], mTestData.getTraverseFromIndex());
            Assert.assertEquals(wordEnds[i], mTestData.getTraverseToIndex());
            Assert.assertEquals(0, mTestData.getSelectionFromIndex());
            Assert.assertEquals(wordEnds[i], mTestData.getSelectionToIndex());
        }

        // Simulate swiping left (backward) (removes from selections)
        for (int i = 4; i >= 0; --i) {
            performActionOnUiThread(editTextVirtualViewId,
                    AccessibilityNodeInfo.ACTION_PREVIOUS_AT_MOVEMENT_GRANULARITY, args);

            Assert.assertEquals(wordStarts[i], mTestData.getTraverseFromIndex());
            Assert.assertEquals(wordEnds[i], mTestData.getTraverseToIndex());
            Assert.assertEquals(0, mTestData.getSelectionFromIndex());
            Assert.assertEquals(wordStarts[i], mTestData.getSelectionToIndex());
        }
    }

    /**
     * Ensure traverse events and selection events are properly indexed when navigating a
     * contenteditable by character with selection mode on.
     */
    @Test
    @LargeTest
    public void testEventIndices_contenteditable_SelectionON_CharacterGranularity()
            throws Throwable {
        setupTestWithHTML("<div contenteditable>Testing</div>");

        // Find a node in the accessibility tree with input type TYPE_CLASS_TEXT.
        int contentEditableVirtualViewId =
                waitForNodeMatching(sClassNameMatcher, "android.widget.EditText");
        mNodeInfo = createAccessibilityNodeInfo(contentEditableVirtualViewId);
        Assert.assertNotNull(NODE_TIMEOUT_ERROR, mNodeInfo);

        focusNode(contentEditableVirtualViewId);

        // Move cursor to the end of the field for consistency.
        Bundle moveArgs = new Bundle();
        moveArgs.putInt(AccessibilityNodeInfo.ACTION_ARGUMENT_MOVEMENT_GRANULARITY_INT,
                AccessibilityNodeInfo.MOVEMENT_GRANULARITY_CHARACTER);
        for (int i = 7; i > 0; i--) {
            performActionOnUiThread(contentEditableVirtualViewId,
                    AccessibilityNodeInfo.ACTION_NEXT_AT_MOVEMENT_GRANULARITY, moveArgs);
        }

        // Set granularity to CHARACTER, with selection TRUE
        Bundle args = new Bundle();
        args.putInt(AccessibilityNodeInfo.ACTION_ARGUMENT_MOVEMENT_GRANULARITY_INT,
                AccessibilityNodeInfo.MOVEMENT_GRANULARITY_CHARACTER);
        args.putBoolean(AccessibilityNodeInfo.ACTION_ARGUMENT_EXTEND_SELECTION_BOOLEAN, true);

        // Simulate swiping left (backward) (adds to selections)
        for (int i = 7; i > 0; i--) {
            performActionOnUiThread(contentEditableVirtualViewId,
                    AccessibilityNodeInfo.ACTION_PREVIOUS_AT_MOVEMENT_GRANULARITY, args);

            Assert.assertEquals(i - 1, mTestData.getTraverseFromIndex());
            Assert.assertEquals(i, mTestData.getTraverseToIndex());
            Assert.assertEquals(7, mTestData.getSelectionFromIndex());
            Assert.assertEquals(i - 1, mTestData.getSelectionToIndex());
        }

        // Simulate swiping right (forward) (removes from selection)
        for (int i = 0; i < 7; i++) {
            performActionOnUiThread(contentEditableVirtualViewId,
                    AccessibilityNodeInfo.ACTION_NEXT_AT_MOVEMENT_GRANULARITY, args);

            Assert.assertEquals(i, mTestData.getTraverseFromIndex());
            Assert.assertEquals(i + 1, mTestData.getTraverseToIndex());
            Assert.assertEquals(7, mTestData.getSelectionFromIndex());
            Assert.assertEquals(i + 1, mTestData.getSelectionToIndex());
        }

        // Turn selection mode off and traverse to beginning so we can select forwards
        args.putBoolean(AccessibilityNodeInfo.ACTION_ARGUMENT_EXTEND_SELECTION_BOOLEAN, false);
        for (int i = 7; i > 0; i--) {
            performActionOnUiThread(contentEditableVirtualViewId,
                    AccessibilityNodeInfo.ACTION_PREVIOUS_AT_MOVEMENT_GRANULARITY, args);
        }

        // Turn selection mode on
        args.putBoolean(AccessibilityNodeInfo.ACTION_ARGUMENT_EXTEND_SELECTION_BOOLEAN, true);

        // Simulate swiping right (forward) (adds to selection)
        for (int i = 0; i < 7; i++) {
            performActionOnUiThread(contentEditableVirtualViewId,
                    AccessibilityNodeInfo.ACTION_NEXT_AT_MOVEMENT_GRANULARITY, args);

            Assert.assertEquals(i, mTestData.getTraverseFromIndex());
            Assert.assertEquals(i + 1, mTestData.getTraverseToIndex());
            Assert.assertEquals(0, mTestData.getSelectionFromIndex());
            Assert.assertEquals(i + 1, mTestData.getSelectionToIndex());
        }

        // Simulate swiping left (backward) (removes from selections)
        for (int i = 7; i > 0; i--) {
            performActionOnUiThread(contentEditableVirtualViewId,
                    AccessibilityNodeInfo.ACTION_PREVIOUS_AT_MOVEMENT_GRANULARITY, args);

            Assert.assertEquals(i - 1, mTestData.getTraverseFromIndex());
            Assert.assertEquals(i, mTestData.getTraverseToIndex());
            Assert.assertEquals(0, mTestData.getSelectionFromIndex());
            Assert.assertEquals(i - 1, mTestData.getSelectionToIndex());
        }
    }

    /**
     * Test |AccessibilityNodeInfo| object for contenteditable node.
     */
    @Test
    @SmallTest
    public void testNodeInfo_className_contenteditable() {
        setupTestWithHTML("<div contenteditable>Edit This</div>");

        int textNodeVirtualViewId =
                waitForNodeMatching(sClassNameMatcher, "android.widget.EditText");
        mNodeInfo = createAccessibilityNodeInfo(textNodeVirtualViewId);

        Assert.assertNotNull(NODE_TIMEOUT_ERROR, mNodeInfo);
        Assert.assertTrue(CONTENTEDITABLE_ERROR, mNodeInfo.isEditable());
        Assert.assertEquals(CONTENTEDITABLE_ERROR, "Edit This", mNodeInfo.getText().toString());
    }

    /**
     * Test |AccessibilityNodeInfo| object for node with aria-invalid="true".
     */
    @Test
    @SmallTest
    public void testNodeInfo_errorMessage_true() {
        setupTestWithHTML("<input type='text' aria-invalid='true' value='123456789'>");

        int textNodeVirtualViewId =
                waitForNodeMatching(sClassNameMatcher, "android.widget.EditText");
        mNodeInfo = createAccessibilityNodeInfo(textNodeVirtualViewId);

        Assert.assertNotNull(NODE_TIMEOUT_ERROR, mNodeInfo);
        Assert.assertTrue(ARIA_INVALID_ERROR, mNodeInfo.isContentInvalid());
        Assert.assertEquals(ARIA_INVALID_ERROR, "Invalid entry", mNodeInfo.getError());
    }

    /**
     * Test |AccessibilityNodeInfo| object for node with aria-invalid="spelling".
     */
    @Test
    @SmallTest
    public void testNodeInfo_errorMessage_spelling() {
        setupTestWithHTML("<input type='text' aria-invalid='spelling' value='123456789'>");

        int textNodeVirtualViewId =
                waitForNodeMatching(sClassNameMatcher, "android.widget.EditText");
        mNodeInfo = createAccessibilityNodeInfo(textNodeVirtualViewId);

        Assert.assertNotNull(NODE_TIMEOUT_ERROR, mNodeInfo);
        Assert.assertTrue(ARIA_INVALID_ERROR, mNodeInfo.isContentInvalid());
        Assert.assertEquals(ARIA_INVALID_ERROR, "Invalid spelling", mNodeInfo.getError());
    }

    /**
     * Test |AccessibilityNodeInfo| object for node with aria-invalid="grammar".
     */
    @Test
    @SmallTest
    public void testNodeInfo_errorMessage_grammar() {
        setupTestWithHTML("<input type='text' aria-invalid='grammar' value='123456789'>");

        int textNodeVirtualViewId =
                waitForNodeMatching(sClassNameMatcher, "android.widget.EditText");
        mNodeInfo = createAccessibilityNodeInfo(textNodeVirtualViewId);

        Assert.assertNotNull(NODE_TIMEOUT_ERROR, mNodeInfo);
        Assert.assertTrue(ARIA_INVALID_ERROR, mNodeInfo.isContentInvalid());
        Assert.assertEquals(ARIA_INVALID_ERROR, "Invalid grammar", mNodeInfo.getError());
    }

    /**
     * Test |AccessibilityNodeInfo| object for node with no aria-invalid.
     */
    @Test
    @SmallTest
    public void testNodeInfo_errorMessage_none() {
        setupTestWithHTML("<input type='text'>");

        int textNodeVirtualViewId =
                waitForNodeMatching(sClassNameMatcher, "android.widget.EditText");
        mNodeInfo = createAccessibilityNodeInfo(textNodeVirtualViewId);

        Assert.assertNotNull(NODE_TIMEOUT_ERROR, mNodeInfo);
        Assert.assertFalse(ARIA_INVALID_ERROR, mNodeInfo.isContentInvalid());
        Assert.assertNull(ARIA_INVALID_ERROR, mNodeInfo.getError());
    }

    /**
     * Test |AccessibilityNodeInfo| object for node with spelling error, and ensure the
     * spelling error is encoded as a Spannable.
     **/
    @Test
    @SmallTest
    public void testNodeInfo_spellingError() {
        setupTestWithHTML("<input type='text' value='one wordd has an error'>");

        // Call a test API to explicitly add a spelling error in the same format as
        // would be generated if spelling correction was enabled. Clear our cache for this node.
        int textNodeVirtualViewId =
                waitForNodeMatching(sClassNameMatcher, "android.widget.EditText");
        mActivityTestRule.mWcax.addSpellingErrorForTesting(textNodeVirtualViewId, 4, 9);
        mActivityTestRule.mWcax.clearNodeInfoCacheForGivenId(textNodeVirtualViewId);

        // Get |AccessibilityNodeInfo| object and confirm it is not null.
        mNodeInfo = createAccessibilityNodeInfo(textNodeVirtualViewId);
        Assert.assertNotNull(NODE_TIMEOUT_ERROR, mNodeInfo);

        // Assert that the node's text has a SuggestionSpan surrounding the proper word.
        CharSequence text = mNodeInfo.getText();
        Assert.assertTrue(SPELLING_ERROR, text instanceof Spannable);

        Spannable spannable = (Spannable) text;
        SuggestionSpan[] spans = spannable.getSpans(0, text.length(), SuggestionSpan.class);
        Assert.assertNotNull(SPELLING_ERROR, spans);
        Assert.assertEquals(SPELLING_ERROR, 1, spans.length);
        Assert.assertEquals(SPELLING_ERROR, 4, spannable.getSpanStart(spans[0]));
        Assert.assertEquals(SPELLING_ERROR, 9, spannable.getSpanEnd(spans[0]));
    }

    /**
     * Test |AccessibilityNodeInfo| object for character bounds for a node in Android O.
     */
    @Test
    @SmallTest
    @MinAndroidSdkLevel(Build.VERSION_CODES.O)
    @TargetApi(Build.VERSION_CODES.O)
    public void testNodeInfo_extraDataAdded() {
        setupTestWithHTML("<h1>Simple test page</h1><section><p>Text</p></section>");

        // Wait until we find a node in the accessibility tree with the text "Text".
        int textNodeVirtualViewId = waitForNodeMatching(sTextMatcher, "Text");
        mNodeInfo = createAccessibilityNodeInfo(textNodeVirtualViewId);
        Assert.assertNotNull(NODE_TIMEOUT_ERROR, mNodeInfo);

        // Call the API we want to test - addExtraDataToAccessibilityNodeInfo.
        final Bundle arguments = new Bundle();
        arguments.putInt(EXTRA_DATA_TEXT_CHARACTER_LOCATION_ARG_START_INDEX, 0);
        arguments.putInt(EXTRA_DATA_TEXT_CHARACTER_LOCATION_ARG_LENGTH, 4);

        // addExtraDataToAccessibilityNodeInfo() will end up calling RenderFrameHostImpl's method
        // AccessibilityPerformAction() in the C++ code, which needs to be run from the UI thread.
        TestThreadUtils.runOnUiThreadBlocking(() -> {
            mActivityTestRule.mNodeProvider.addExtraDataToAccessibilityNodeInfo(
                    textNodeVirtualViewId, mNodeInfo, EXTRA_DATA_TEXT_CHARACTER_LOCATION_KEY,
                    arguments);
        });

        // It should return a result, but all of the rects will be the same because it hasn't
        // loaded inline text boxes yet.
        Bundle extras = mNodeInfo.getExtras();
        RectF[] result =
                (RectF[]) extras.getParcelableArray(EXTRA_DATA_TEXT_CHARACTER_LOCATION_KEY);
        Assert.assertNotEquals(result, null);
        Assert.assertEquals(result.length, 4);
        Assert.assertEquals(result[0], result[1]);
        Assert.assertEquals(result[0], result[2]);
        Assert.assertEquals(result[0], result[3]);

        // The role string should be a camel cased programmatic identifier.
        CharSequence roleString = extras.getCharSequence("AccessibilityNodeInfo.chromeRole");
        Assert.assertEquals("paragraph", roleString.toString());

        // The data needed for text character locations loads asynchronously. Block until
        // it successfully returns the character bounds.
        CriteriaHelper.pollUiThread(() -> {
            AccessibilityNodeInfo textNode = createAccessibilityNodeInfo(textNodeVirtualViewId);
            mActivityTestRule.mNodeProvider.addExtraDataToAccessibilityNodeInfo(
                    textNodeVirtualViewId, textNode, EXTRA_DATA_TEXT_CHARACTER_LOCATION_KEY,
                    arguments);
            Bundle textNodeExtras = textNode.getExtras();
            RectF[] textNodeResults = (RectF[]) textNodeExtras.getParcelableArray(
                    EXTRA_DATA_TEXT_CHARACTER_LOCATION_KEY);
            Criteria.checkThat(textNodeResults, Matchers.arrayWithSize(4));
            Criteria.checkThat(textNodeResults[0], Matchers.not(textNodeResults[1]));
        });

        // The final result should be the separate bounding box of all four characters.
        mNodeInfo = createAccessibilityNodeInfo(textNodeVirtualViewId);
        TestThreadUtils.runOnUiThreadBlocking(() -> {
            mActivityTestRule.mNodeProvider.addExtraDataToAccessibilityNodeInfo(
                    textNodeVirtualViewId, mNodeInfo, EXTRA_DATA_TEXT_CHARACTER_LOCATION_KEY,
                    arguments);
        });

        extras = mNodeInfo.getExtras();
        result = (RectF[]) extras.getParcelableArray(EXTRA_DATA_TEXT_CHARACTER_LOCATION_KEY);
        Assert.assertNotEquals(result[0], result[1]);
        Assert.assertNotEquals(result[0], result[2]);
        Assert.assertNotEquals(result[0], result[3]);

        // All four should have nonzero left, top, width, and height
        for (int i = 0; i < 4; ++i) {
            Assert.assertTrue(result[i].left > 0);
            Assert.assertTrue(result[i].top > 0);
            Assert.assertTrue(result[i].width() > 0);
            Assert.assertTrue(result[i].height() > 0);
        }

        // They should be in order.
        Assert.assertTrue(result[0].left < result[1].left);
        Assert.assertTrue(result[1].left < result[2].left);
        Assert.assertTrue(result[2].left < result[3].left);
    }

    /**
     * Test |AccessibilityNodeInfo| object actions to ensure we are not adding ACTION_LONG_CLICK
     * to nodes due to verbose utterances issue.
     */
    @Test
    @SmallTest
    @MinAndroidSdkLevel(Build.VERSION_CODES.M)
    public void testNodeInfo_noLongClickAction() {
        // Build a simple web page with a node.
        setupTestWithHTML("<p>Example paragraph</p>");

        int textViewId = waitForNodeMatching(sTextOrContentDescriptionMatcher, "Example paragraph");
        mNodeInfo = createAccessibilityNodeInfo(textViewId);
        Assert.assertNotNull(NODE_TIMEOUT_ERROR, mNodeInfo);

        // Confirm the ACTION_LONG_CLICK action has not been added to the node.
        Assert.assertFalse(LONG_CLICK_ERROR, mNodeInfo.getActionList().contains(ACTION_LONG_CLICK));
    }

    /**
     * Test |AccessibilityNodeInfo| object actions for text node.
     */
    @Test
    @SmallTest
    @MinAndroidSdkLevel(Build.VERSION_CODES.M)
    public void testNodeInfo_Actions_SetText() {
        // Load a web page with a text field.
        setupTestWithHTML("<input type='text'>");

        int textNodeVirtualViewId =
                waitForNodeMatching(sClassNameMatcher, "android.widget.EditText");
        mNodeInfo = createAccessibilityNodeInfo(textNodeVirtualViewId);
        Assert.assertNotNull(NODE_TIMEOUT_ERROR, mNodeInfo);

        // Confirm the ACTION_SET_TEXT action has been added to the node.
        Assert.assertTrue(ACTION_SET_ERROR, mNodeInfo.getActionList().contains(ACTION_SET_TEXT));
    }

    /**
     * Test |AccessibilityNodeInfo| object actions for node is specifically user scrollable,
     * and not just programmatically scrollable.
     */
    @Test
    @SmallTest
    @MinAndroidSdkLevel(Build.VERSION_CODES.M)
    public void testNodeInfo_Actions_OverflowHidden() throws Throwable {
        // Build a simple web page with a div and overflow:hidden
        setupTestWithHTML("<div title='1234' style='overflow:hidden; width: 200px; height:50px'>\n"
                + "  <p>Example Paragraph 1</p>\n"
                + "  <p>Example Paragraph 2</p>\n"
                + "</div>");

        // Define our root node and paragraph node IDs by looking for their text.
        int vvIdDiv = waitForNodeMatching(sTextMatcher, "1234");
        int vvIdP1 = waitForNodeMatching(sTextMatcher, "Example Paragraph 1");
        int vvIdP2 = waitForNodeMatching(sTextMatcher, "Example Paragraph 2");

        // Get the |AccessibilityNodeInfo| objects for our nodes.
        AccessibilityNodeInfo nodeInfoDiv = createAccessibilityNodeInfo(vvIdDiv);
        AccessibilityNodeInfo nodeInfoP1 = createAccessibilityNodeInfo(vvIdP1);
        AccessibilityNodeInfo nodeInfoP2 = createAccessibilityNodeInfo(vvIdP2);

        // Assert we have the correct nodes.
        Assert.assertNotNull(NODE_TIMEOUT_ERROR, nodeInfoDiv);
        Assert.assertNotNull(NODE_TIMEOUT_ERROR, nodeInfoP1);
        Assert.assertNotNull(NODE_TIMEOUT_ERROR, nodeInfoP2);

        // Assert the scroll actions are not present in any of the objects.
        assertActionsContainNoScrolls(nodeInfoDiv);
        assertActionsContainNoScrolls(nodeInfoP1);
        assertActionsContainNoScrolls(nodeInfoP2);

        // Traverse to the next node, then re-assert.
        performActionOnUiThread(vvIdDiv, ACTION_NEXT_HTML_ELEMENT, new Bundle());
        assertActionsContainNoScrolls(nodeInfoDiv);
        assertActionsContainNoScrolls(nodeInfoP1);
        assertActionsContainNoScrolls(nodeInfoP2);

        // Repeat.
        performActionOnUiThread(vvIdP1, ACTION_NEXT_HTML_ELEMENT, new Bundle());
        assertActionsContainNoScrolls(nodeInfoDiv);
        assertActionsContainNoScrolls(nodeInfoP1);
        assertActionsContainNoScrolls(nodeInfoP2);
    }

    /**
     * Test |AccessibilityNodeInfo| object actions for node is user scrollable.
     */
    @Test
    @SmallTest
    @MinAndroidSdkLevel(Build.VERSION_CODES.M)
    public void testNodeInfo_Actions_OverflowScroll() throws Throwable {
        // Build a simple web page with a div and overflow:scroll
        setupTestWithHTML("<div title='1234' style='overflow:scroll; width: 200px; height:50px'>\n"
                + "  <p>Example Paragraph 1</p>\n"
                + "  <p>Example Paragraph 2</p>\n"
                + "</div>");

        // Define our root node and paragraph node IDs by looking for their text.
        int vvIdDiv = waitForNodeMatching(sTextMatcher, "1234");
        int vvIdP1 = waitForNodeMatching(sTextMatcher, "Example Paragraph 1");
        int vvIdP2 = waitForNodeMatching(sTextMatcher, "Example Paragraph 2");

        // Get the |AccessibilityNodeInfo| objects for our nodes.
        AccessibilityNodeInfo nodeInfoDiv = createAccessibilityNodeInfo(vvIdDiv);
        AccessibilityNodeInfo nodeInfoP1 = createAccessibilityNodeInfo(vvIdP1);
        AccessibilityNodeInfo nodeInfoP2 = createAccessibilityNodeInfo(vvIdP2);

        // Assert we have the correct nodes.
        Assert.assertNotNull(NODE_TIMEOUT_ERROR, nodeInfoDiv);
        Assert.assertNotNull(NODE_TIMEOUT_ERROR, nodeInfoP1);
        Assert.assertNotNull(NODE_TIMEOUT_ERROR, nodeInfoP2);

        // Assert the scroll actions ARE present for our div node, but not the others.
        Assert.assertTrue(nodeInfoDiv.getActionList().contains(ACTION_SCROLL_FORWARD));
        Assert.assertTrue(nodeInfoDiv.getActionList().contains(ACTION_SCROLL_DOWN));
        assertActionsContainNoScrolls(nodeInfoP1);
        assertActionsContainNoScrolls(nodeInfoP2);

        // Traverse to the next node, then re-assert.
        performActionOnUiThread(vvIdDiv, ACTION_NEXT_HTML_ELEMENT, new Bundle());
        Assert.assertTrue(nodeInfoDiv.getActionList().contains(ACTION_SCROLL_FORWARD));
        Assert.assertTrue(nodeInfoDiv.getActionList().contains(ACTION_SCROLL_DOWN));
        assertActionsContainNoScrolls(nodeInfoP1);
        assertActionsContainNoScrolls(nodeInfoP2);

        // Repeat.
        performActionOnUiThread(vvIdP1, ACTION_NEXT_HTML_ELEMENT, new Bundle());
        Assert.assertTrue(nodeInfoDiv.getActionList().contains(ACTION_SCROLL_FORWARD));
        Assert.assertTrue(nodeInfoDiv.getActionList().contains(ACTION_SCROLL_DOWN));
        assertActionsContainNoScrolls(nodeInfoP1);
        assertActionsContainNoScrolls(nodeInfoP2);
    }

    @MinAndroidSdkLevel(Build.VERSION_CODES.M)
    private void assertActionsContainNoScrolls(AccessibilityNodeInfo nodeInfo) {
        Assert.assertFalse(nodeInfo.getActionList().contains(ACTION_SCROLL_FORWARD));
        Assert.assertFalse(nodeInfo.getActionList().contains(ACTION_SCROLL_BACKWARD));
        Assert.assertFalse(nodeInfo.getActionList().contains(ACTION_SCROLL_UP));
        Assert.assertFalse(nodeInfo.getActionList().contains(ACTION_SCROLL_DOWN));
        Assert.assertFalse(nodeInfo.getActionList().contains(ACTION_SCROLL_LEFT));
        Assert.assertFalse(nodeInfo.getActionList().contains(ACTION_SCROLL_RIGHT));
    }

    private String thresholdError(int count, int max) {
        return THRESHOLD_ERROR + " Received " + count + ", but expected no more than: " + max;
    }

    private String lowThresholdError(int count) {
        return THRESHOLD_LOW_EVENT_COUNT_ERROR + " Received " + count
                + ", but expected at least: " + UNSUPPRESSED_EXPECTED_COUNT;
    }
}
