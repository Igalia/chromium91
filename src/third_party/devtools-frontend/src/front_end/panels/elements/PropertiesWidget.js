// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/*
 * Copyright (C) 2007 Apple Inc.  All rights reserved.
 * Copyright (C) 2014 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1.  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 * 2.  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 * 3.  Neither the name of Apple Computer, Inc. ("Apple") nor the names of
 *     its contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE AND ITS CONTRIBUTORS "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL APPLE OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

import * as Common from '../../core/common/common.js';  // eslint-disable-line no-unused-vars
import * as Host from '../../core/host/host.js';
import * as SDK from '../../core/sdk/sdk.js';
import * as ObjectUI from '../../object_ui/object_ui.js';
import * as UI from '../../ui/legacy/legacy.js';

/** @type {!PropertiesWidget} */
let propertiesWidgetInstance;

export class PropertiesWidget extends UI.ThrottledWidget.ThrottledWidget {
  constructor() {
    super(true /* isWebComponent */);
    this.registerRequiredCSS('panels/elements/propertiesWidget.css', {enableLegacyPatching: false});

    SDK.SDKModel.TargetManager.instance().addModelListener(
        SDK.DOMModel.DOMModel, SDK.DOMModel.Events.AttrModified, this._onNodeChange, this);
    SDK.SDKModel.TargetManager.instance().addModelListener(
        SDK.DOMModel.DOMModel, SDK.DOMModel.Events.AttrRemoved, this._onNodeChange, this);
    SDK.SDKModel.TargetManager.instance().addModelListener(
        SDK.DOMModel.DOMModel, SDK.DOMModel.Events.CharacterDataModified, this._onNodeChange, this);
    SDK.SDKModel.TargetManager.instance().addModelListener(
        SDK.DOMModel.DOMModel, SDK.DOMModel.Events.ChildNodeCountUpdated, this._onNodeChange, this);
    UI.Context.Context.instance().addFlavorChangeListener(SDK.DOMModel.DOMNode, this._setNode, this);
    this._node = UI.Context.Context.instance().flavor(SDK.DOMModel.DOMNode);

    this._treeOutline = new ObjectUI.ObjectPropertiesSection.ObjectPropertiesSectionsTreeOutline({readOnly: true});
    this._treeOutline.setShowSelectionOnKeyboardFocus(/* show */ true, /* preventTabOrder */ false);
    this._expandController =
        new ObjectUI.ObjectPropertiesSection.ObjectPropertiesSectionsTreeExpandController(this._treeOutline);
    this.contentElement.appendChild(this._treeOutline.element);

    this._treeOutline.addEventListener(UI.TreeOutline.Events.ElementExpanded, () => {
      Host.userMetrics.actionTaken(Host.UserMetrics.Action.DOMPropertiesExpanded);
    });

    this.update();
  }
  /**
   * @param {{forceNew: ?boolean}=} opts
   * @return {!PropertiesWidget}
   */
  static instance(opts = {forceNew: null}) {
    const {forceNew} = opts;
    if (!propertiesWidgetInstance || forceNew) {
      propertiesWidgetInstance = new PropertiesWidget();
    }

    return propertiesWidgetInstance;
  }

  /**
   * @param {!Common.EventTarget.EventTargetEvent} event
   */
  _setNode(event) {
    this._node = /** @type {?SDK.DOMModel.DOMNode} */ (event.data);
    this.update();
  }

  /**
   * @override
   * @protected
   * @return {!Promise<void>}
   */
  async doUpdate() {
    if (this._lastRequestedNode) {
      this._lastRequestedNode.domModel().runtimeModel().releaseObjectGroup(_objectGroupName);
      delete this._lastRequestedNode;
    }

    if (!this._node) {
      this._treeOutline.removeChildren();
      return;
    }

    this._lastRequestedNode = this._node;
    const object = await this._node.resolveToObject(_objectGroupName);
    if (!object) {
      return;
    }

    const result = await object.callFunction(protoList);
    object.release();

    if (!result.object || result.wasThrown) {
      return;
    }

    const propertiesResult = await result.object.getOwnProperties(false /* generatePreview */);
    result.object.release();

    if (!propertiesResult || !propertiesResult.properties) {
      return;
    }

    const properties = propertiesResult.properties;
    this._treeOutline.removeChildren();

    let selected = false;
    // Get array of property user-friendly names.
    for (let i = 0; i < properties.length; ++i) {
      if (!parseInt(properties[i].name, 10)) {
        continue;
      }
      const property = properties[i].value;
      if (!property) {
        continue;
      }
      let title = property.description;
      if (!title) {
        continue;
      }
      title = title.replace(/Prototype$/, '');

      const section = this._createSectionTreeElement(property, title);
      this._treeOutline.appendChild(section);
      if (!selected) {
        section.select(/* omitFocus= */ true, /* selectedByUser= */ false);
        selected = true;
      }
    }

    /**
     * @this {*}
     */
    function protoList() {
      let proto = this;
      /** @type {!Object<(number|string), *>} */
      const result = {__proto__: null};
      let counter = 1;
      while (proto) {
        result[counter++] = proto;
        proto = proto.__proto__;
      }
      return result;
    }
  }

  /**
   * @param {!SDK.RemoteObject.RemoteObject} property
   * @param {string} title
   * @returns {!ObjectUI.ObjectPropertiesSection.RootElement}
   */
  _createSectionTreeElement(property, title) {
    const titleElement = document.createElement('span');
    titleElement.classList.add('tree-element-title');
    titleElement.textContent = title;

    const section = new ObjectUI.ObjectPropertiesSection.RootElement(property);
    section.title = titleElement;
    this._expandController.watchSection(title, section);

    return section;
  }

  /**
   * @param {!Common.EventTarget.EventTargetEvent} event
   */
  _onNodeChange(event) {
    if (!this._node) {
      return;
    }
    const data = event.data;
    const node = /** @type {!SDK.DOMModel.DOMNode} */ (data instanceof SDK.DOMModel.DOMNode ? data : data.node);
    if (this._node !== node) {
      return;
    }
    this.update();
  }
}

export const _objectGroupName = 'properties-sidebar-pane';
