/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.samples.browser.downloads

import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.feature.downloads.AbstractFetchDownloadService
import mozilla.components.feature.downloads.DownloadEstimator
import mozilla.components.feature.downloads.FileSizeFormatter
import mozilla.components.support.base.android.NotificationsDelegate
import org.mozilla.samples.browser.ext.components

class DownloadService : AbstractFetchDownloadService() {
    override val httpClient by lazy { components.client }
    override val store: BrowserStore by lazy { components.store }
    override val notificationsDelegate: NotificationsDelegate by lazy { components.notificationsDelegate }
    override val fileSizeFormatter: FileSizeFormatter by lazy { components.fileSizeFormatter }
    override val downloadEstimator: DownloadEstimator by lazy { components.downloadEstimator }
}
