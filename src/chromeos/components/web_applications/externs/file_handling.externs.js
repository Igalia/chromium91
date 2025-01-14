// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * @fileoverview @externs
 * Externs for interfaces in //third_party/blink/renderer/modules/launch/*
 * This file can be removed when upstreamed to the closure compiler.
 */

/** @interface */
class FileSystemWriter {
  /**
   * @param {number} position
   * @param {BufferSource|Blob|string} data
   * @return {!Promise<undefined>}
   */
  async write(position, data) {}

  /**
   * @param {number} size
   * @return {!Promise<undefined>}
   */
  async truncate(size) {}

  /**
   * @return {!Promise<undefined>}
   */
  async close() {}
}

/** @typedef {{type: string}} */
let GetSystemDirectoryOptions;

/** @interface */
class LaunchParams {
  constructor() {
    /** @type {Array<FileSystemHandle>} */
    this.files;

    /** @type {Request} */
    this.request;
  }
}

/** @typedef {function(LaunchParams)} */
let LaunchConsumer;

/** @interface */
class LaunchQueue {
  /** @param {LaunchConsumer} consumer */
  setConsumer(consumer) {}
}

/**
 * https://wicg.github.io/native-file-system/#native-filesystem
 * @param {(!FilePickerOptions|undefined)} options
 * @return {!Promise<(!Array<!FileSystemFileHandle>)>}
 */
window.showOpenFilePicker;

/**
 * https://wicg.github.io/native-file-system/#native-filesystem
 * @param {(!FilePickerOptions|undefined)} options
 * @return {!Promise<(!FileSystemFileHandle)>}
 */
window.showSaveFilePicker;

/**
 * https://wicg.github.io/native-file-system/#native-filesystem
 * @param {(!FilePickerOptions|undefined)} options
 * @return {!Promise<(!FileSystemDirectoryHandle)>}
 */
window.showDirectoryPicker;

/** @type {LaunchQueue} */
window.launchQueue;
