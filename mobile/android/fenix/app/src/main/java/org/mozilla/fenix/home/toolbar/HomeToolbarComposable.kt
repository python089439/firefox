/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home.toolbar

import android.content.Context
import android.view.Gravity
import android.view.ViewGroup
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.ComposeView
import androidx.coordinatorlayout.widget.CoordinatorLayout
import androidx.core.view.updateLayoutParams
import androidx.fragment.app.Fragment
import androidx.lifecycle.DefaultLifecycleObserver
import androidx.lifecycle.LifecycleOwner
import androidx.navigation.NavController
import mozilla.components.browser.state.ext.getUrl
import mozilla.components.browser.state.selector.findTab
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.compose.base.Divider
import mozilla.components.compose.browser.toolbar.BrowserToolbar
import mozilla.components.compose.browser.toolbar.store.BrowserEditToolbarAction.SearchQueryUpdated
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarState
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarStore
import mozilla.components.compose.browser.toolbar.store.EnvironmentCleared
import mozilla.components.compose.browser.toolbar.store.EnvironmentRehydrated
import mozilla.components.support.ktx.android.view.ImeInsetsSynchronizer
import org.mozilla.fenix.R
import org.mozilla.fenix.browser.browsingmode.BrowsingModeManager
import org.mozilla.fenix.components.AppStore
import org.mozilla.fenix.components.StoreProvider
import org.mozilla.fenix.components.appstate.AppAction.SearchAction.SearchStarted
import org.mozilla.fenix.components.metrics.MetricsUtils
import org.mozilla.fenix.components.toolbar.ToolbarPosition.BOTTOM
import org.mozilla.fenix.components.toolbar.ToolbarPosition.TOP
import org.mozilla.fenix.databinding.FragmentHomeBinding
import org.mozilla.fenix.ext.components
import org.mozilla.fenix.search.BrowserToolbarSearchMiddleware
import org.mozilla.fenix.search.BrowserToolbarSearchStatusSyncMiddleware
import org.mozilla.fenix.theme.FirefoxTheme
import org.mozilla.fenix.utils.Settings

/**
 * A wrapper over the [BrowserToolbar] composable to allow for extra customisation and
 * integration in the same framework as the [HomeToolbarView].
 *
 * @param context [Context] used for various system interactions.
 * @param lifecycleOwner [Fragment] as a [LifecycleOwner] to used to organize lifecycle dependent operations.
 * @param navController [NavController] to use for navigating to other in-app destinations.
 * @param homeBinding [FragmentHomeBinding] which will serve as parent for this composable.
 * @param appStore [AppStore] to sync from.
 * @param browserStore [BrowserStore] to sync from.
 * @param browsingModeManager [BrowsingModeManager] for querying the current browsing mode.
 * @param settings [Settings] for querying various application settings.
 * @param directToSearchConfig [DirectToSearchConfig] configuration for starting with the toolbar in search mode.
 * @param tabStripContent [Composable] as the tab strip content to be displayed together with this toolbar.
 * @param searchSuggestionsContent [Composable] as the search suggestions content to be displayed
 * together with this toolbar.
 * @param navigationBarContent Composable content for the navigation bar.
 */
@Suppress("LongParameterList")
internal class HomeToolbarComposable(
    private val context: Context,
    private val lifecycleOwner: Fragment,
    private val navController: NavController,
    private val homeBinding: FragmentHomeBinding,
    private val appStore: AppStore,
    private val browserStore: BrowserStore,
    private val browsingModeManager: BrowsingModeManager,
    private val settings: Settings,
    private val directToSearchConfig: DirectToSearchConfig,
    private val tabStripContent: @Composable () -> Unit,
    private val searchSuggestionsContent: @Composable (BrowserToolbarStore, Modifier) -> Unit,
    private val navigationBarContent: (@Composable () -> Unit)?,
) : FenixHomeToolbar {
    private var showDivider by mutableStateOf(true)

    private val store = initializeToolbarStore()

    override val layout = ComposeView(context).apply {
        id = R.id.composable_toolbar

        setContent {
            val shouldShowTabStrip: Boolean = remember { settings.isTabStripEnabled }

            FirefoxTheme {
                Column {
                    if (shouldShowTabStrip) {
                        tabStripContent()
                    }

                    if (settings.shouldUseBottomToolbar) {
                        searchSuggestionsContent(store, Modifier.weight(1f))
                    }
                    BrowserToolbar(showDivider, settings.shouldUseBottomToolbar)
                    if (settings.toolbarPosition == BOTTOM) {
                        navigationBarContent?.invoke()
                    }
                    if (!settings.shouldUseBottomToolbar) {
                        searchSuggestionsContent(store, Modifier.weight(1f))
                    }
                }
            }
        }
        translationZ = context.resources.getDimension(R.dimen.browser_fragment_above_toolbar_panels_elevation)
        homeBinding.homeLayout.addView(this)
    }

    override fun build(browserState: BrowserState) {
        layout.updateLayoutParams {
            (this as? CoordinatorLayout.LayoutParams)?.gravity = when (settings.toolbarPosition) {
                TOP -> Gravity.TOP
                BOTTOM -> Gravity.BOTTOM
            }
        }

        if (settings.shouldUseBottomToolbar) {
            ImeInsetsSynchronizer.setup(layout)
        }

        updateHomeAppBarIntegration()
        configureStartingInSearchMode()
    }

    override fun updateDividerVisibility(isVisible: Boolean) {
        showDivider = isVisible
    }

    override fun updateButtonVisibility(
        browserState: BrowserState,
    ) {
        // To be added later
    }

    override fun updateTabCounter(browserState: BrowserState) {
        // To be added later
    }

    override fun updateAddressBarVisibility(isVisible: Boolean) {
        // To be added later
    }

    @Composable
    private fun BrowserToolbar(shouldShowDivider: Boolean, shouldUseBottomToolbar: Boolean) {
        // Ensure the divider is shown together with the toolbar
        Box {
            BrowserToolbar(
                store = store,
            )
            if (shouldShowDivider) {
                Divider(
                    modifier = Modifier.align(
                        when (shouldUseBottomToolbar) {
                            true -> Alignment.TopCenter
                            false -> Alignment.BottomCenter
                        },
                    ),
                )
            }
        }
    }

    private fun updateHomeAppBarIntegration() {
        if (!settings.shouldUseBottomToolbar) {
            homeBinding.homeAppBar.updateLayoutParams<ViewGroup.MarginLayoutParams> {
                topMargin = context.resources.getDimensionPixelSize(R.dimen.home_fragment_top_toolbar_header_margin) +
                    when (settings.isTabStripEnabled) {
                        true -> context.resources.getDimensionPixelSize(R.dimen.tab_strip_height)
                        false -> 0
                    }
            }
        }
    }

    private fun configureStartingInSearchMode() {
        if (!directToSearchConfig.startSearch) return
        appStore.dispatch(
            SearchStarted(
                tabId = directToSearchConfig.sessionId,
                source = directToSearchConfig.source,
            ),
        )

        if (directToSearchConfig.sessionId != null) {
            browserStore.state.findTab(directToSearchConfig.sessionId)?.let {
                store.dispatch(
                    SearchQueryUpdated(
                        query = it.getUrl() ?: "",
                        showAsPreselected = true,
                    ),
                )
            }
        }
    }

    private fun initializeToolbarStore() = StoreProvider.get(lifecycleOwner) {
        BrowserToolbarStore(
            initialState = BrowserToolbarState(),
            middleware = listOf(
                BrowserToolbarSearchStatusSyncMiddleware(appStore),
                BrowserToolbarMiddleware(
                    appStore = appStore,
                    browserStore = browserStore,
                    clipboard = context.components.clipboardHandler,
                    useCases = context.components.useCases,
                ),
                BrowserToolbarSearchMiddleware(
                    appStore = appStore,
                    browserStore = browserStore,
                    components = context.components,
                    settings = context.components.settings,
                ),
            ),
        )
    }.also {
        it.dispatch(
            EnvironmentRehydrated(
                HomeToolbarEnvironment(
                    context = context,
                    viewLifecycleOwner = lifecycleOwner.viewLifecycleOwner,
                    navController = navController,
                    browsingModeManager = browsingModeManager,
                ),
            ),
        )

        lifecycleOwner.viewLifecycleOwner.lifecycle.addObserver(
            object : DefaultLifecycleObserver {
                override fun onDestroy(owner: LifecycleOwner) {
                    it.dispatch(EnvironmentCleared)
                }
            },
        )
    }

    /**
     * Static configuration and properties of [HomeToolbarComposable].
     */
    companion object {
        /**
         * Configuration for starting with the toolbar in search mode.
         *
         * @property startSearch Whether to start in search mode. Defaults to `false`.
         * @property sessionId The session ID of the current session with details of which to start search.
         * Defaults to `null`.
         * @property source The application feature from where a new search was started.
         */
        data class DirectToSearchConfig(
            val startSearch: Boolean = false,
            val sessionId: String? = null,
            val source: MetricsUtils.Source = MetricsUtils.Source.NONE,
        )
    }
}
