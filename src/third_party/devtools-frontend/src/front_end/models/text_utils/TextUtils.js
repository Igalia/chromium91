/*
 * Copyright (C) 2013 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

import * as Platform from '../../core/platform/platform.js';

import {SearchMatch} from './ContentProvider.js';
import {Text} from './Text.js';

export const Utils = {
  get _keyValueFilterRegex() {
    return /(?:^|\s)(\-)?([\w\-]+):([^\s]+)/;
  },
  get _regexFilterRegex() {
    return /(?:^|\s)(\-)?\/([^\s]+)\//;
  },
  get _textFilterRegex() {
    return /(?:^|\s)(\-)?([^\s]+)/;
  },
  get _SpaceCharRegex() {
    return /\s/;
  },
  /**
   * @enum {string}
   */
  get Indent() {
    return {TwoSpaces: '  ', FourSpaces: '    ', EightSpaces: '        ', TabCharacter: '\t'};
  },

  /**
   * @param {string} char
   * @return {boolean}
   */
  isStopChar: function(char) {
    return (char > ' ' && char < '0') || (char > '9' && char < 'A') || (char > 'Z' && char < '_') ||
        (char > '_' && char < 'a') || (char > 'z' && char <= '~');
  },

  /**
   * @param {string} char
   * @return {boolean}
   */
  isWordChar: function(char) {
    return !Utils.isStopChar(char) && !Utils.isSpaceChar(char);
  },

  /**
   * @param {string} char
   * @return {boolean}
   */
  isSpaceChar: function(char) {
    return Utils._SpaceCharRegex.test(char);
  },

  /**
   * @param {string} word
   * @return {boolean}
   */
  isWord: function(word) {
    for (let i = 0; i < word.length; ++i) {
      if (!Utils.isWordChar(word.charAt(i))) {
        return false;
      }
    }
    return true;
  },

  /**
   * @param {string} char
   * @return {boolean}
   */
  isOpeningBraceChar: function(char) {
    return char === '(' || char === '{';
  },

  /**
   * @param {string} char
   * @return {boolean}
   */
  isClosingBraceChar: function(char) {
    return char === ')' || char === '}';
  },

  /**
   * @param {string} char
   * @return {boolean}
   */
  isBraceChar: function(char) {
    return Utils.isOpeningBraceChar(char) || Utils.isClosingBraceChar(char);
  },

  /**
   * @param {string} text
   * @param {function(string):boolean} isWordChar
   * @param {function(string)} wordCallback
   */
  textToWords: function(text, isWordChar, wordCallback) {
    let startWord = -1;
    for (let i = 0; i < text.length; ++i) {
      if (!isWordChar(text.charAt(i))) {
        if (startWord !== -1) {
          wordCallback(text.substring(startWord, i));
        }
        startWord = -1;
      } else if (startWord === -1) {
        startWord = i;
      }
    }
    if (startWord !== -1) {
      wordCallback(text.substring(startWord));
    }
  },

  /**
   * @param {string} line
   * @return {string}
   */
  lineIndent: function(line) {
    let indentation = 0;
    while (indentation < line.length && Utils.isSpaceChar(line.charAt(indentation))) {
      ++indentation;
    }
    return line.substr(0, indentation);
  },

  /**
   * @param {string} text
   * @return {boolean}
   */
  isUpperCase: function(text) {
    return text === text.toUpperCase();
  },

  /**
   * @param {string} text
   * @return {boolean}
   */
  isLowerCase: function(text) {
    return text === text.toLowerCase();
  },

  /**
   * @param {string} text
   * @param {!Array<!RegExp>} regexes
   * @return {!Array<{value: string, position: number, regexIndex: number, captureGroups: !Array<string|undefined>}>}
   */
  splitStringByRegexes(text, regexes) {
    /** @type {!Array<{value: string, position: number, regexIndex: number, captureGroups: !Array<string|undefined>}>} */
    const matches = [];
    /** @type {!Array<!RegExp>} */
    const globalRegexes = [];
    for (let i = 0; i < regexes.length; i++) {
      const regex = regexes[i];
      if (!regex.global) {
        globalRegexes.push(new RegExp(regex.source, regex.flags ? regex.flags + 'g' : 'g'));
      } else {
        globalRegexes.push(regex);
      }
    }
    doSplit(text, 0, 0);
    return matches;

    /**
     * @param {string} text
     * @param {number} regexIndex
     * @param {number} startIndex
     */
    function doSplit(text, regexIndex, startIndex) {
      if (regexIndex >= globalRegexes.length) {
        // Set regexIndex as -1 if text did not match with any regular expression
        matches.push({value: text, position: startIndex, regexIndex: -1, captureGroups: []});
        return;
      }
      const regex = globalRegexes[regexIndex];
      let currentIndex = 0;
      let result;
      regex.lastIndex = 0;
      while ((result = regex.exec(text)) !== null) {
        const stringBeforeMatch = text.substring(currentIndex, result.index);
        if (stringBeforeMatch) {
          doSplit(stringBeforeMatch, regexIndex + 1, startIndex + currentIndex);
        }
        const match = result[0];
        matches.push({
          value: match,
          position: startIndex + result.index,
          regexIndex: regexIndex,
          captureGroups: result.slice(1)
        });
        currentIndex = result.index + match.length;
      }
      const stringAfterMatches = text.substring(currentIndex);
      if (stringAfterMatches) {
        doSplit(stringAfterMatches, regexIndex + 1, startIndex + currentIndex);
      }
    }
  }
};

export class FilterParser {
  /**
   * @param {!Array<string>} keys
   */
  constructor(keys) {
    this._keys = keys;
  }

  /**
   * @param {!ParsedFilter} filter
   * @return {!ParsedFilter}
   */
  static cloneFilter(filter) {
    return {key: filter.key, text: filter.text, regex: filter.regex, negative: filter.negative};
  }

  /**
   * @param {string} query
   * @return {!Array<!ParsedFilter>}
   */
  parse(query) {
    const splitResult = Utils.splitStringByRegexes(
        query, [Utils._keyValueFilterRegex, Utils._regexFilterRegex, Utils._textFilterRegex]);
    /** @type {!Array<!ParsedFilter>} */
    const filters = [];
    for (let i = 0; i < splitResult.length; i++) {
      const regexIndex = splitResult[i].regexIndex;
      if (regexIndex === -1) {
        continue;
      }
      const result = splitResult[i].captureGroups;
      if (regexIndex === 0) {
        if (this._keys.indexOf(/** @type {string} */ (result[1])) !== -1) {
          filters.push({key: result[1], regex: undefined, text: result[2], negative: Boolean(result[0])});
        } else {
          filters.push(
              {key: undefined, regex: undefined, text: result[1] + ':' + result[2], negative: Boolean(result[0])});
        }
      } else if (regexIndex === 1) {
        try {
          filters.push({
            key: undefined,
            regex: new RegExp(/** @type {string} */ (result[1]), 'i'),
            text: undefined,
            negative: Boolean(result[0])
          });
        } catch (e) {
          filters.push({key: undefined, regex: undefined, text: '/' + result[1] + '/', negative: Boolean(result[0])});
        }
      } else if (regexIndex === 2) {
        filters.push({key: undefined, regex: undefined, text: result[1], negative: Boolean(result[0])});
      }
    }
    return filters;
  }
}

export class BalancedJSONTokenizer {
  /**
   * @param {function(string):void} callback
   * @param {boolean=} findMultiple
   */
  constructor(callback, findMultiple) {
    this._callback = callback;
    /** @type {number} */
    this._index = 0;
    this._balance = 0;
    /** @type {string} */
    this._buffer = '';
    this._findMultiple = findMultiple || false;
    this._closingDoubleQuoteRegex = /[^\\](?:\\\\)*"/g;
  }

  /**
   * @param {string} chunk
   * @return {boolean}
   */
  write(chunk) {
    this._buffer += chunk;
    const lastIndex = this._buffer.length;
    const buffer = this._buffer;
    let index;
    for (index = this._index; index < lastIndex; ++index) {
      const character = buffer[index];
      if (character === '"') {
        this._closingDoubleQuoteRegex.lastIndex = index;
        if (!this._closingDoubleQuoteRegex.test(buffer)) {
          break;
        }
        index = this._closingDoubleQuoteRegex.lastIndex - 1;
      } else if (character === '{') {
        ++this._balance;
      } else if (character === '}') {
        --this._balance;
        if (this._balance < 0) {
          this._reportBalanced();
          return false;
        }
        if (!this._balance) {
          this._lastBalancedIndex = index + 1;
          if (!this._findMultiple) {
            break;
          }
        }
      } else if (character === ']' && !this._balance) {
        this._reportBalanced();
        return false;
      }
    }
    this._index = index;
    this._reportBalanced();
    return true;
  }

  _reportBalanced() {
    if (!this._lastBalancedIndex) {
      return;
    }
    this._callback(this._buffer.slice(0, this._lastBalancedIndex));
    this._buffer = this._buffer.slice(this._lastBalancedIndex);
    this._index -= this._lastBalancedIndex;
    this._lastBalancedIndex = 0;
  }

  /**
   * @return {string}
   */
  remainder() {
    return this._buffer;
  }
}

/**
 * @interface
 */
export class TokenizerFactory {
  /**
   * @param {string} mimeType
   * @param {!CodeMirror.Mode<*>=} mode
   * @return {function(string, function(string, ?string, number, number):void):void}
   */
  createTokenizer(mimeType, mode) {
    throw new Error('not implemented');
  }
}

/**
 * @param {string} text
 * @return {boolean}
 */
export function isMinified(text) {
  const kMaxNonMinifiedLength = 500;
  let linesToCheck = 10;
  let lastPosition = 0;
  do {
    let eolIndex = text.indexOf('\n', lastPosition);
    if (eolIndex < 0) {
      eolIndex = text.length;
    }
    if (eolIndex - lastPosition > kMaxNonMinifiedLength && text.substr(lastPosition, 3) !== '//#') {
      return true;
    }
    lastPosition = eolIndex + 1;
  } while (--linesToCheck >= 0 && lastPosition < text.length);

  // Check the end of the text as well
  linesToCheck = 10;
  lastPosition = text.length;
  do {
    let eolIndex = text.lastIndexOf('\n', lastPosition);
    if (eolIndex < 0) {
      eolIndex = 0;
    }
    if (lastPosition - eolIndex > kMaxNonMinifiedLength && text.substr(lastPosition, 3) !== '//#') {
      return true;
    }
    lastPosition = eolIndex - 1;
  } while (--linesToCheck >= 0 && lastPosition > 0);
  return false;
}

/**
 * @param {string} content
 * @param {string} query
 * @param {boolean} caseSensitive
 * @param {boolean} isRegex
 * @return {!Array.<!SearchMatch>}
 */
export const performSearchInContent = function(content, query, caseSensitive, isRegex) {
  const regex = Platform.StringUtilities.createSearchRegex(query, caseSensitive, isRegex);

  const text = new Text(content);
  const result = [];
  for (let i = 0; i < text.lineCount(); ++i) {
    const lineContent = text.lineAt(i);
    regex.lastIndex = 0;
    if (regex.exec(lineContent)) {
      result.push(new SearchMatch(i, lineContent));
    }
  }
  return result;
};

/** @typedef {{key:(string|undefined), text:(?string|undefined), regex:(!RegExp|undefined), negative:boolean}} */
// @ts-ignore typedef
export let ParsedFilter;
