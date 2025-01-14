// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// eslint-disable-next-line rulesdir/es_modules_import
import * as UI from '../legacy/legacy.js';
import {DataGridController} from './DataGridController.js';
import type {DataGridControllerData} from './DataGridController.js';

export class DataGridControllerIntegrator extends UI.Widget.VBox {
  readonly dataGrid: DataGridController;

  constructor(data: DataGridControllerData) {
    /**
     * first true = isWebComponent and tells the widget system it's rendering a
     * component
     * second true = delegatesFocus, which tells the widget system to
     * let the component deal with its own focusing.
     */
    super(true, true);

    this.dataGrid = new DataGridController();
    this.dataGrid.data = data;
    this.contentElement.appendChild(this.dataGrid);
  }

  data(): Readonly<DataGridControllerData> {
    return this.dataGrid.data;
  }

  update(data: Readonly<DataGridControllerData>): void {
    this.dataGrid.data = data;
  }
}
