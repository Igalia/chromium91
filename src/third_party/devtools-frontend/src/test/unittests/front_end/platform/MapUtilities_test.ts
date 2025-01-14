// Copyright (c) 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import * as Platform from '../../../../front_end/core/platform/platform.js';

const {assert} = chai;

describe('MapUtilities', () => {
  describe('inverse', () => {
    it('inverts the map returning a multimap with the map\'s values as keys and the map\'s keys as values', () => {
      const pairs: readonly(readonly[string, number])[] = [
        ['a', 1],
        ['b', 2],
        ['c', 3],
        ['d', 1],
      ];
      const map = new Map(pairs);

      const inverse = Platform.MapUtilities.inverse(map);
      for (const [, value] of pairs) {
        assert.sameMembers([...inverse.get(value)], [...getKeys(value)]);
      }

      function getKeys(lookupValue: number): Set<string> {
        const keys = new Set<string>();
        for (const [key, value] of pairs) {
          if (value === lookupValue) {
            keys.add(key);
          }
        }
        return keys;
      }
    });
  });
});
