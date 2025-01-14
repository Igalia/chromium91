// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.components.feature_engagement;

/**
 * EventConstants contains the String name of all in-product help events.
 */
public final class EventConstants {
    /**
     * The page load has failed and user has landed on an offline dino page.
     */
    public static final String USER_HAS_SEEN_DINO = "user_has_seen_dino";

    /**
     * The user has started downloading a page.
     */
    public static final String DOWNLOAD_PAGE_STARTED = "download_page_started";

    /**
     * The download has completed successfully.
     */
    public static final String DOWNLOAD_COMPLETED = "download_completed";

    /**
     * The download home was opened by the user (from toolbar menu or notifications).
     */
    public static final String DOWNLOAD_HOME_OPENED = "download_home_opened";

    /**
     * Screenshot is taken with Chrome in the foreground.
     */
    public static final String SCREENSHOT_TAKEN_CHROME_IN_FOREGROUND =
            "screenshot_taken_chrome_in_foreground";

    /**
     * The data saver preview infobar was shown.
     */
    public static final String DATA_SAVER_PREVIEW_INFOBAR_SHOWN = "data_saver_preview_opened";

    /**
     * Data was saved when page loaded.
     */
    public static final String DATA_SAVED_ON_PAGE_LOAD = "data_saved_page_load";

    /**
     * The overflow menu was opened.
     */
    public static final String OVERFLOW_OPENED_WITH_DATA_SAVER_SHOWN =
            "overflow_opened_data_saver_shown";

    /**
     * The data saver footer was used (tapped).
     */
    public static final String DATA_SAVER_DETAIL_OPENED = "data_saver_overview_opened";

    /**
     * The data saver milestone promo was used (tapped).
     */
    public static final String DATA_SAVER_MILESTONE_PROMO_OPENED = "data_saver_milestone_promo";

    /**
     * The previews verbose status view was opened.
     */
    public static final String PREVIEWS_VERBOSE_STATUS_OPENED = "previews_verbose_status_opened";

    /**
     * A page load used a preview.
     */
    public static final String PREVIEWS_PAGE_LOADED = "preview_page_load";

    /**
     * Add to homescreen events.
     */
    public static final String ADD_TO_HOMESCREEN_DIALOG_SHOWN = "add_to_homescreen_dialog_shown";

    /**
     * Contextual Search panel was opened.
     */
    public static final String CONTEXTUAL_SEARCH_PANEL_OPENED = "contextual_search_panel_opened";

    /**
     * Contextual Search panel was opened after it was triggered by tapping.
     */
    public static final String CONTEXTUAL_SEARCH_PANEL_OPENED_AFTER_TAP =
            "contextual_search_panel_opened_after_tap";

    /**
     * Contextual Search panel was opened after it was triggered by longpressing.
     */
    public static final String CONTEXTUAL_SEARCH_PANEL_OPENED_AFTER_LONGPRESS =
            "contextual_search_panel_opened_after_longpress";

    /**
     * Contextual Search panel was opened after receiving entity data.
     */
    public static final String CONTEXTUAL_SEARCH_PANEL_OPENED_FOR_ENTITY =
            "contextual_search_panel_opened_for_entity";

    /**
     * User performed a web search for a query by choosing the Web Search option on the popup menu.
     */
    public static final String WEB_SEARCH_PERFORMED = "web_search_performed";

    /**
     * Contextual Search showed an entity result for the searched query.
     */
    public static final String CONTEXTUAL_SEARCH_ENTITY_RESULT = "contextual_search_entity_result";

    /**
     * Contextual Search was triggered by a tap.
     */
    public static final String CONTEXTUAL_SEARCH_TRIGGERED_BY_TAP =
            "contextual_search_triggered_by_tap";

    /**
     * Contextual Search was triggered by longpressing.
     */
    public static final String CONTEXTUAL_SEARCH_TRIGGERED_BY_LONGPRESS =
            "contextual_search_triggered_by_longpress";

    /**
     * Contextual Search attempted-trigger by Tap when user should Long-press.
     */
    public static final String CONTEXTUAL_SEARCH_TAPPED_BUT_SHOULD_LONGPRESS =
            "contextual_search_tapped_but_should_longpress";

    /**
     * Contextual Search acknowledged the suggestion that they should longpress instead of tap.
     */
    public static final String CONTEXTUAL_SEARCH_ACKNOWLEDGED_IN_PANEL_HELP =
            "contextual_search_acknowledged_in_panel_help";

    /**
     * Contextual Search user fully enabled access to page content through the opt-in.
     */
    public static final String CONTEXTUAL_SEARCH_ENABLED_OPT_IN =
            "contextual_search_enabled_opt_in";

    /**
     * The partner homepage was pressed.
     */
    public static final String PARTNER_HOME_PAGE_BUTTON_PRESSED =
            "partner_home_page_button_pressed";

    /** The homepage button in the toolbar was clicked. */
    public static final String HOMEPAGE_BUTTON_CLICKED = "homepage_button_clicked";

    /** The clear tab button in the toolbar was clicked. */
    public static final String CLEAR_TAB_BUTTON_CLICKED = "clear_tab_button_clicked";

    /** The pinned homepage tile in MV tiles was clicked. */
    public static final String HOMEPAGE_TILE_CLICKED = "homepage_tile_clicked";

    /** The `Translate` app menu button was clicked. */
    public static final String TRANSLATE_MENU_BUTTON_CLICKED = "translate_menu_button_clicked";

    /** The keyboard accessory was used to fill address data into a form. */
    public static final String KEYBOARD_ACCESSORY_ADDRESS_AUTOFILLED =
            "keyboard_accessory_address_suggestion_accepted";

    /** The keyboard accessory was used to fill a password form. */
    public static final String KEYBOARD_ACCESSORY_PASSWORD_AUTOFILLED =
            "keyboard_accessory_password_suggestion_accepted";

    /** The keyboard accessory was used to fill payment data into a form. */
    public static final String KEYBOARD_ACCESSORY_PAYMENT_AUTOFILLED =
            "keyboard_accessory_payment_suggestion_accepted";

    /** The keyboard accessory was swiped to reveal more suggestions. */
    public static final String KEYBOARD_ACCESSORY_BAR_SWIPED = "keyboard_accessory_bar_swiped";

    /** The Explore Sites tile was tapped. */
    public static final String EXPLORE_SITES_TILE_TAPPED = "explore_sites_tile_tapped";

    /** User has finished drop-to-merge to create a group. */
    public static final String TAB_DRAG_AND_DROP_TO_GROUP = "tab_drag_and_drop_to_group";

    /** User has tapped on Identity Disc. */
    public static final String IDENTITY_DISC_USED = "identity_disc_used";

    /** User has used Ephemeral Tab i.e. opened and browsed the content. */
    public static final String EPHEMERAL_TAB_USED = "ephemeral_tab_used";

    /** HomepagePromo has been accepted. */
    public static final String NTP_SHOWN = "ntp_shown";
    public static final String NTP_HOME_BUTTON_CLICKED = "ntp_homebutton_clicked";

    public static final String TAB_SWITCHER_BUTTON_CLICKED = "tab_switcher_button_clicked";

    /** Read later related events. */
    public static final String APP_MENU_BOOKMARK_STAR_ICON_PRESSED =
            "app_menu_bookmark_star_icon_pressed";
    public static final String READ_LATER_CONTEXT_MENU_TAPPED = "read_later_context_menu_tapped";
    public static final String READ_LATER_ARTICLE_SAVED = "read_later_article_saved";
    public static final String READ_LATER_BOTTOM_SHEET_FOLDER_SEEN =
            "read_later_bottom_sheet_folder_seen";
    public static final String READ_LATER_BOOKMARK_FOLDER_OPENED =
            "read_later_bookmark_folder_opened";

    /** Video tutorial related events. */
    public static final String VIDEO_TUTORIAL_DISMISSED_SUMMARY =
            "video_tutorial_iph_dismissed_summary";
    public static final String VIDEO_TUTORIAL_DISMISSED_CHROME_INTRO =
            "video_tutorial_iph_dismissed_chrome_intro";
    public static final String VIDEO_TUTORIAL_DISMISSED_DOWNLOAD =
            "video_tutorial_iph_dismissed_download";
    public static final String VIDEO_TUTORIAL_DISMISSED_SEARCH =
            "video_tutorial_iph_dismissed_search";
    public static final String VIDEO_TUTORIAL_DISMISSED_VOICE_SEARCH =
            "video_tutorial_iph_dismissed_voice_search";
    public static final String VIDEO_TUTORIAL_CLICKED_SUMMARY =
            "video_tutorial_iph_clicked_summary";
    public static final String VIDEO_TUTORIAL_CLICKED_CHROME_INTRO =
            "video_tutorial_iph_clicked_chrome_intro";
    public static final String VIDEO_TUTORIAL_CLICKED_DOWNLOAD =
            "video_tutorial_iph_clicked_download";
    public static final String VIDEO_TUTORIAL_CLICKED_SEARCH = "video_tutorial_iph_clicked_search";
    public static final String VIDEO_TUTORIAL_CLICKED_VOICE_SEARCH =
            "video_tutorial_iph_clicked_voice_search";

    /** Reengagement events. */
    public static final String STARTED_FROM_MAIN_INTENT = "started_from_main_intent";

    /** PWA install events. */
    public static final String PWA_INSTALL_MENU_SELECTED = "pwa_install_menu_clicked";

    /** PageInfo events. */
    public static final String PAGE_INFO_OPENED = "page_info_opened";

    /** Permission events. */
    public static final String PERMISSION_REQUEST_SHOWN = "permission_request_shown";

    /** Screenshot events */
    public static final String SHARE_SCREENSHOT_SELECTED = "share_screenshot_clicked";

    /** Mic toolbar IPH event */
    public static final String SUCCESSFUL_VOICE_SEARCH = "successful_voice_search";

    /**
     * Do not instantiate.
     */
    private EventConstants() {}
}
