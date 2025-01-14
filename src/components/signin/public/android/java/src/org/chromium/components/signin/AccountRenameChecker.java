// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.components.signin;

import android.accounts.Account;
import android.content.Context;

import androidx.annotation.Nullable;
import androidx.annotation.WorkerThread;

import com.google.android.gms.auth.AccountChangeEvent;
import com.google.android.gms.auth.GoogleAuthException;
import com.google.android.gms.auth.GoogleAuthUtil;

import org.chromium.base.ContextUtils;
import org.chromium.base.Log;

import java.io.IOException;
import java.util.List;

/**
 * A checker of the account rename event.
 */
public final class AccountRenameChecker {
    private static final String TAG = "AccountRenameChecker";

    private static final class SystemDelegate {
        /**
         * Gets the new account name of the renamed account.
         * @return The new name that the given account email is renamed to or null if the given
         *         email is not renamed.
         */
        @WorkerThread
        @Nullable
        String getNewNameOfRenamedAccount(String accountEmail) {
            final Context context = ContextUtils.getApplicationContext();
            try {
                final List<AccountChangeEvent> accountChangeEvents =
                        GoogleAuthUtil.getAccountChangeEvents(context, 0, accountEmail);
                for (AccountChangeEvent event : accountChangeEvents) {
                    if (event.getChangeType() == GoogleAuthUtil.CHANGE_TYPE_ACCOUNT_RENAMED_TO) {
                        return event.getChangeData();
                    }
                }
            } catch (IOException | GoogleAuthException e) {
                Log.w(TAG, "Failed to get change events", e);
            }
            return null;
        }
    }

    private final SystemDelegate mDelegate;

    public AccountRenameChecker() {
        mDelegate = new SystemDelegate();
    }

    /**
     * Gets the new account name of the renamed account.
     * @return If the old account email is renamed to an account that exists in the given list of
     *         accounts, the renamed-to account name will be returned. Otherwise it returns null.
     */
    @WorkerThread
    public @Nullable String getNewNameOfRenamedAccount(
            String oldAccountEmail, List<Account> accounts) {
        String newAccountEmail = mDelegate.getNewNameOfRenamedAccount(oldAccountEmail);
        while (newAccountEmail != null) {
            if (AccountUtils.findAccountByName(accounts, newAccountEmail) != null) {
                break;
            }
            // When the new name does not exist in the list, continue to search if it is
            // renamed to another account existing in the list.
            newAccountEmail = mDelegate.getNewNameOfRenamedAccount(newAccountEmail);
        }
        return newAccountEmail;
    }
}
