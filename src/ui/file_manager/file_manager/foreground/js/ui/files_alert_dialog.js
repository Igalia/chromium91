// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// #import {util} from '../../../common/js/util.m.js';
// #import {AlertDialog} from 'chrome://resources/js/cr/ui/dialogs.m.js';

/**
 * Alert dialog.
 */
/* #export */ class FilesAlertDialog extends cr.ui.dialogs.AlertDialog {
  /**
   * @param {!HTMLElement} parentNode
   */
  constructor(parentNode) {
    super(parentNode);

    if (util.isFilesNg()) {
      this.container.classList.add('files-ng');
    }
  }

  /**
   * @protected
   * @override
   */
  initDom() {
    super.initDom();
    super.hasModalContainer = true;

    this.frame.classList.add('files-alert-dialog');
  }

  /**
   * @override
   * @suppress {accessControls}
   */
  show_(...args) {
    this.parentNode_ = util.getFilesAppModalDialogInstance();

    super.show_(...args);

    this.parentNode_.showModal();
  }

  /**
   * @override
   */
  hide(...args) {
    this.parentNode_.close();

    super.hide(...args);
  }

  /**
   * @override
   */
  showWithTitle(title, message, ...args) {
    this.frame.classList.toggle('no-title', !title);
    super.showWithTitle(title, message, ...args);
  }

  /**
   * @override
   */
  showHtml(title, message, ...args) {
    this.frame.classList.toggle('no-title', !title);
    super.showHtml(title, message, ...args);
  }
}
