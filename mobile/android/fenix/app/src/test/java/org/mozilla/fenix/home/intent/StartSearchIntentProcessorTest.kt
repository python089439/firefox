/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home.intent

import android.content.Intent
import androidx.navigation.NavController
import androidx.navigation.navOptions
import io.mockk.Called
import io.mockk.every
import io.mockk.mockk
import io.mockk.verify
import mozilla.components.support.test.robolectric.testContext
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.GleanMetrics.SearchWidget
import org.mozilla.fenix.HomeActivity
import org.mozilla.fenix.NavGraphDirections
import org.mozilla.fenix.R
import org.mozilla.fenix.components.metrics.MetricsUtils
import org.mozilla.fenix.helpers.FenixGleanTestRule
import org.mozilla.fenix.utils.Settings
import org.robolectric.RobolectricTestRunner

@RunWith(RobolectricTestRunner::class)
class StartSearchIntentProcessorTest {

    @get:Rule
    val gleanTestRule = FenixGleanTestRule(testContext)

    private val navController: NavController = mockk(relaxed = true)
    private val out: Intent = mockk(relaxed = true)
    private val settings: Settings = mockk {
        every { shouldUseComposableToolbar } returns false
    }

    @Test
    fun `do not process blank intents`() {
        verify { navController wasNot Called }
        verify { out wasNot Called }
    }

    @Test
    fun `do not process when search extra is false`() {
        val intent = Intent().apply {
            removeExtra(HomeActivity.OPEN_TO_SEARCH)
        }
        StartSearchIntentProcessor().process(intent, navController, out, settings)

        verify { navController wasNot Called }
        verify { out wasNot Called }
    }

    @Test
    fun `process search intents`() {
        val intent = Intent().apply {
            putExtra(HomeActivity.OPEN_TO_SEARCH, StartSearchIntentProcessor.SEARCH_WIDGET)
        }
        StartSearchIntentProcessor().process(intent, navController, out, settings)
        val options = navOptions {
            popUpTo(R.id.homeFragment)
        }

        assertNotNull(SearchWidget.newTabButton.testGetValue())
        val recordedEvents = SearchWidget.newTabButton.testGetValue()!!
        assertEquals(1, recordedEvents.size)
        assertEquals(null, recordedEvents.single().extra)

        verify {
            navController.navigate(
                NavGraphDirections.actionGlobalSearchDialog(
                    sessionId = null,
                    searchAccessPoint = MetricsUtils.Source.WIDGET,
                ),
                options,
            )
        }
        verify { out.removeExtra(HomeActivity.OPEN_TO_SEARCH) }
    }

    @Test
    fun `process search intents to open new search UX`() {
        every { settings.shouldUseComposableToolbar } returns true
        val intent = Intent().apply {
            putExtra(HomeActivity.OPEN_TO_SEARCH, StartSearchIntentProcessor.SEARCH_WIDGET)
        }
        StartSearchIntentProcessor().process(intent, navController, out, settings)

        assertNotNull(SearchWidget.newTabButton.testGetValue())
        val recordedEvents = SearchWidget.newTabButton.testGetValue()!!
        assertEquals(1, recordedEvents.size)
        assertEquals(null, recordedEvents.single().extra)

        verify {
            navController.navigate(
                NavGraphDirections.actionGlobalHome(
                    focusOnAddressBar = true,
                    searchAccessPoint = MetricsUtils.Source.WIDGET,
                ),
                null,
            )
        }
        verify { out.removeExtra(HomeActivity.OPEN_TO_SEARCH) }
    }
}
