/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.search

import android.os.Handler
import android.os.Looper
import androidx.lifecycle.Lifecycle.State.RESUMED
import androidx.lifecycle.LifecycleOwner
import androidx.navigation.NavController
import androidx.navigation.NavDirections
import io.mockk.Runs
import io.mockk.every
import io.mockk.just
import io.mockk.mockk
import io.mockk.verify
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.android.asCoroutineDispatcher
import kotlinx.coroutines.test.setMain
import mozilla.components.browser.state.action.AwesomeBarAction.EngagementFinished
import mozilla.components.browser.state.action.SearchAction.ApplicationSearchEnginesLoaded
import mozilla.components.browser.state.search.RegionState
import mozilla.components.browser.state.search.SearchEngine
import mozilla.components.browser.state.state.SearchState
import mozilla.components.browser.state.state.selectedOrDefaultSearchEngine
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.compose.browser.toolbar.concept.Action.SearchSelectorAction
import mozilla.components.compose.browser.toolbar.store.BrowserEditToolbarAction.SearchAborted
import mozilla.components.compose.browser.toolbar.store.BrowserEditToolbarAction.SearchQueryUpdated
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarAction.ToggleEditMode
import mozilla.components.compose.browser.toolbar.store.BrowserToolbarStore
import mozilla.components.compose.browser.toolbar.store.EnvironmentCleared
import mozilla.components.compose.browser.toolbar.store.EnvironmentRehydrated
import mozilla.components.concept.toolbar.AutocompleteProvider
import mozilla.components.support.test.ext.joinBlocking
import mozilla.components.support.test.middleware.CaptureActionsMiddleware
import mozilla.components.support.test.mock
import mozilla.components.support.test.robolectric.testContext
import mozilla.telemetry.glean.testing.GleanTestRule
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.GleanMetrics.UnifiedSearch
import org.mozilla.fenix.browser.BrowserFragmentDirections
import org.mozilla.fenix.components.AppStore
import org.mozilla.fenix.components.Components
import org.mozilla.fenix.components.appstate.AppAction
import org.mozilla.fenix.components.appstate.AppAction.SearchAction.SearchEnded
import org.mozilla.fenix.components.appstate.AppAction.SearchAction.SearchStarted
import org.mozilla.fenix.components.appstate.AppState
import org.mozilla.fenix.components.appstate.search.SelectedSearchEngine
import org.mozilla.fenix.components.search.BOOKMARKS_SEARCH_ENGINE_ID
import org.mozilla.fenix.components.search.HISTORY_SEARCH_ENGINE_ID
import org.mozilla.fenix.components.search.TABS_SEARCH_ENGINE_ID
import org.mozilla.fenix.helpers.lifecycle.TestLifecycleOwner
import org.mozilla.fenix.home.toolbar.HomeToolbarEnvironment
import org.mozilla.fenix.search.SearchSelectorEvents.SearchSelectorClicked
import org.mozilla.fenix.search.SearchSelectorEvents.SearchSelectorItemClicked
import org.mozilla.fenix.search.SearchSelectorEvents.SearchSettingsItemClicked
import org.mozilla.fenix.search.ext.searchEngineShortcuts
import org.mozilla.fenix.search.fixtures.assertSearchSelectorEquals
import org.mozilla.fenix.search.fixtures.buildExpectedSearchSelector
import org.mozilla.fenix.utils.Settings
import org.robolectric.RobolectricTestRunner
import org.robolectric.Shadows.shadowOf
import org.mozilla.fenix.components.appstate.search.SearchState as AppSearchState

@RunWith(RobolectricTestRunner::class)
class BrowserToolbarSearchMiddlewareTest {
    @get:Rule
    val gleanTestRule = GleanTestRule(testContext)

    val appStore = AppStore()
    val browserStore: BrowserStore = mockk(relaxed = true) {
        every { state.search } returns fakeSearchState()
    }
    val components: Components = mockk()
    val settings: Settings = mockk(relaxed = true)
    val lifecycleOwner: LifecycleOwner = TestLifecycleOwner(RESUMED)
    val navController: NavController = mockk {
        every { navigate(any<NavDirections>()) } just Runs
    }

    @Test
    fun `GIVEN an environment was already set WHEN it is cleared THEN reset it to null`() {
        val (middleware, store) = buildMiddlewareAndAddToStore()

        assertNotNull(middleware.environment)

        store.dispatch(EnvironmentCleared)

        assertNull(middleware.environment)
        assertEquals(emptyList<AutocompleteProvider>(), store.state.editState.autocompleteProviders)
    }

    @Test
    fun `WHEN the toolbar enters in edit mode THEN a new search selector button is added`() {
        val (_, store) = buildMiddlewareAndAddToStore()

        store.dispatch(ToggleEditMode(true))

        assertSearchSelectorEquals(
            expectedSearchSelector(),
            store.state.editState.editActionsStart[0] as SearchSelectorAction,
        )
    }

    @Test
    fun `WHEN the search selector button is clicked THEN record a telemetry event`() {
        val (_, store) = buildMiddlewareAndAddToStore()

        store.dispatch(SearchSelectorClicked)

        assertNotNull(UnifiedSearch.searchMenuTapped.testGetValue())
    }

    @Test
    fun `GIVEN the search selector menu is open WHEN the search settings button is clicked THEN exit edit mode and open search settings`() {
        val captorMiddleware = CaptureActionsMiddleware<AppState, AppAction>()
        val appStore = AppStore(middlewares = listOf(captorMiddleware))
        val (_, store) = buildMiddlewareAndAddToStore(appStore = appStore)
        appStore.dispatch(SearchStarted())
        store.dispatch(ToggleEditMode(true))
        store.dispatch(SearchQueryUpdated("test"))
        assertTrue(store.state.isEditMode())
        assertTrue(appStore.state.searchState.isSearchActive)
        assertEquals("test", store.state.editState.query)

        store.dispatch(SearchSettingsItemClicked)

        assertFalse(appStore.state.searchState.isSearchActive)
        assertEquals("", store.state.editState.query)
        captorMiddleware.assertLastAction(SearchEnded::class) {}
        verify { browserStore.dispatch(EngagementFinished(abandoned = true)) }
        verify {
            navController.navigate(
                BrowserFragmentDirections.actionGlobalSearchEngineFragment(),
            )
        }
    }

    @Test
    fun `GIVEN the search selector menu is open WHEN a menu item is clicked THEN update the selected search engine and rebuild the menu`() {
        val (_, store) = buildMiddlewareAndAddToStore()
        val newEngineSelection = fakeSearchState().searchEngineShortcuts.last()
        store.dispatch(ToggleEditMode(true))
        assertSearchSelectorEquals(
            expectedSearchSelector(),
            store.state.editState.editActionsStart[0] as SearchSelectorAction,
        )

        store.dispatch(SearchSelectorItemClicked(newEngineSelection))

        assertSearchSelectorEquals(
            expectedSearchSelector(newEngineSelection),
            store.state.editState.editActionsStart[0] as SearchSelectorAction,
        )
    }

    @Test
    fun `GIVEN the search selector menu is open while in display mode WHEN a menu item is clicked THEN enter edit mode`() {
        val (_, store) = buildMiddlewareAndAddToStore()
        val newEngineSelection = fakeSearchState().searchEngineShortcuts.last()
        store.dispatch(ToggleEditMode(false))
        assertFalse(store.state.isEditMode())

        store.dispatch(SearchSelectorItemClicked(newEngineSelection))

        assertTrue(appStore.state.searchState.isSearchActive)
    }

    @Test
    fun `GIVEN default engine selected WHEN entering in edit mode THEN set autocomplete providers`() {
        every { settings.shouldAutocompleteInAwesomebar } returns true
        every { settings.shouldShowHistorySuggestions } returns true
        every { settings.shouldShowBookmarkSuggestions } returns true
        configureAutocompleteProvidersInComponents()
        val (_, store) = buildMiddlewareAndAddToStore()

        store.dispatch(ToggleEditMode(true))

        assertEquals(
            listOf(
                components.core.historyStorage,
                components.core.bookmarksStorage,
                components.core.domainsAutocompleteProvider,
            ),
            store.state.editState.autocompleteProviders,
        )
    }

    @Test
    fun `GIVEN default engine selected and history suggestions disabled WHEN entering in edit mode THEN set autocomplete providers`() {
        every { settings.shouldAutocompleteInAwesomebar } returns true
        every { settings.shouldShowHistorySuggestions } returns false
        every { settings.shouldShowBookmarkSuggestions } returns true
        configureAutocompleteProvidersInComponents()
        val (_, store) = buildMiddlewareAndAddToStore()

        store.dispatch(ToggleEditMode(true))

        assertEquals(
            listOf(
                components.core.bookmarksStorage,
                components.core.domainsAutocompleteProvider,
            ),
            store.state.editState.autocompleteProviders,
        )
    }

    @Test
    fun `GIVEN default engine selected and bookmarks suggestions disabled WHEN entering in edit mode THEN set autocomplete providers`() {
        every { settings.shouldAutocompleteInAwesomebar } returns true
        every { settings.shouldShowHistorySuggestions } returns true
        every { settings.shouldShowBookmarkSuggestions } returns false
        configureAutocompleteProvidersInComponents()
        val (_, store) = buildMiddlewareAndAddToStore()

        store.dispatch(ToggleEditMode(true))

        assertEquals(
            listOf(
                components.core.historyStorage,
                components.core.domainsAutocompleteProvider,
            ),
            store.state.editState.autocompleteProviders,
        )
    }

    @Test
    fun `GIVEN default engine selected and history + bookmarks suggestions disabled WHEN entering in edit mode THEN set autocomplete providers`() {
        every { settings.shouldAutocompleteInAwesomebar } returns true
        every { settings.shouldShowHistorySuggestions } returns false
        every { settings.shouldShowBookmarkSuggestions } returns false
        configureAutocompleteProvidersInComponents()
        val (_, store) = buildMiddlewareAndAddToStore()

        store.dispatch(ToggleEditMode(true))

        assertEquals(
            listOf(components.core.domainsAutocompleteProvider),
            store.state.editState.autocompleteProviders,
        )
    }

    @Test
    fun `GIVEN tabs engine selected WHEN entering in edit mode THEN set autocomplete providers`() {
        every { settings.shouldAutocompleteInAwesomebar } returns true
        every { settings.shouldShowHistorySuggestions } returns true
        every { settings.shouldShowBookmarkSuggestions } returns true
        configureAutocompleteProvidersInComponents()
        val appStore = AppStore()
        val (_, store) = buildMiddlewareAndAddToStore(appStore = appStore)

        store.dispatch(
            SearchSelectorItemClicked(
                fakeSearchState().applicationSearchEngines.first { it.id == TABS_SEARCH_ENGINE_ID },
            ),
        ).joinBlocking()

        assertEquals(
            listOf(
                components.core.sessionAutocompleteProvider,
                components.backgroundServices.syncedTabsAutocompleteProvider,
            ),
            store.state.editState.autocompleteProviders,
        )
    }

    @Test
    fun `GIVEN bookmarks engine selected WHEN entering in edit mode THEN set autocomplete providers`() {
        every { settings.shouldAutocompleteInAwesomebar } returns true
        every { settings.shouldShowHistorySuggestions } returns true
        every { settings.shouldShowBookmarkSuggestions } returns true
        configureAutocompleteProvidersInComponents()
        val appStore = AppStore()
        val (_, store) = buildMiddlewareAndAddToStore(appStore = appStore)

        store.dispatch(
            SearchSelectorItemClicked(
                fakeSearchState().applicationSearchEngines.first { it.id == BOOKMARKS_SEARCH_ENGINE_ID },
            ),
        ).joinBlocking()

        assertEquals(
            listOf(components.core.bookmarksStorage),
            store.state.editState.autocompleteProviders,
        )
    }

    @Test
    fun `GIVEN history engine selected WHEN entering in edit mode THEN set autocomplete providers`() {
        every { settings.shouldAutocompleteInAwesomebar } returns true
        every { settings.shouldShowHistorySuggestions } returns true
        every { settings.shouldShowBookmarkSuggestions } returns true
        configureAutocompleteProvidersInComponents()
        val appStore = AppStore()
        val (_, store) = buildMiddlewareAndAddToStore(appStore = appStore)

        store.dispatch(
            SearchSelectorItemClicked(
                fakeSearchState().applicationSearchEngines.first { it.id == HISTORY_SEARCH_ENGINE_ID },
            ),
        ).joinBlocking()

        assertEquals(
            listOf(components.core.historyStorage),
            store.state.editState.autocompleteProviders,
        )
    }

    @Test
    fun `GIVEN other search engine selected WHEN entering in edit mode THEN set autocomplete providers`() {
        every { settings.shouldAutocompleteInAwesomebar } returns true
        configureAutocompleteProvidersInComponents()
        val (_, store) = buildMiddlewareAndAddToStore()

        store.dispatch(SearchSelectorItemClicked(mockk(relaxed = true))).joinBlocking()
        store.dispatch(ToggleEditMode(true))

        assertEquals(
            emptyList<AutocompleteProvider>(),
            store.state.editState.autocompleteProviders,
        )
    }

    @Test
    fun `WHEN the search engines are updated in BrowserStore THEN update the search selector and search providers`() {
        Dispatchers.setMain(Handler(Looper.getMainLooper()).asCoroutineDispatcher())

        val browserStore = BrowserStore()
        val (_, store) = buildMiddlewareAndAddToStore(browserStore = browserStore)
        store.dispatch(ToggleEditMode(true))
        val newSearchEngines = fakeSearchState().applicationSearchEngines

        browserStore.dispatch(ApplicationSearchEnginesLoaded(newSearchEngines)).joinBlocking()
        shadowOf(Looper.getMainLooper()).idle() // wait for observing and processing the search engines update

        assertSearchSelectorEquals(
            expectedSearchSelector(newSearchEngines[0], newSearchEngines),
            store.state.editState.editActionsStart[0] as SearchSelectorAction,
        )
    }

    @Test
    fun `GIVEN a search engine is already selected WHEN the search engines are updated in BrowserStore THEN don't change the selected search engine`() {
        Dispatchers.setMain(Handler(Looper.getMainLooper()).asCoroutineDispatcher())

        val selectedSearchEngine = fakeSearchState().applicationSearchEngines.first().copy(id = "test")
        val appStore = AppStore(
            AppState(
                searchState = AppSearchState.EMPTY.copy(
                    selectedSearchEngine = SelectedSearchEngine(selectedSearchEngine, true),
                ),
            ),
        )
        val browserStore = BrowserStore()
        val (_, store) = buildMiddlewareAndAddToStore(appStore, browserStore)
        store.dispatch(ToggleEditMode(true))
        val newSearchEngines = fakeSearchState().applicationSearchEngines

        browserStore.dispatch(ApplicationSearchEnginesLoaded(newSearchEngines)).joinBlocking()
        shadowOf(Looper.getMainLooper()).idle() // wait for observing and processing the search engines update

        assertSearchSelectorEquals(
            expectedSearchSelector(selectedSearchEngine, newSearchEngines),
            store.state.editState.editActionsStart[0] as SearchSelectorAction,
        )
    }

    @Test
    fun `WHEN the search is aborted THEN sync this in application and browser state`() {
        val appStore: AppStore = mockk(relaxed = true)
        val browserStore: BrowserStore = mockk(relaxed = true)
        val (_, store) = buildMiddlewareAndAddToStore(appStore, browserStore)

        store.dispatch(SearchAborted)

        verify { appStore.dispatch(SearchEnded) }
        verify { browserStore.dispatch(EngagementFinished(abandoned = true)) }
    }

    private fun expectedSearchSelector(
        defaultOrSelectedSearchEngine: SearchEngine = fakeSearchState().selectedOrDefaultSearchEngine!!,
        searchEngineShortcuts: List<SearchEngine> = fakeSearchState().searchEngineShortcuts,
    ) = buildExpectedSearchSelector(
        defaultOrSelectedSearchEngine,
        searchEngineShortcuts,
        testContext.resources,
    )

    private fun buildMiddlewareAndAddToStore(
        appStore: AppStore = this.appStore,
        browserStore: BrowserStore = this.browserStore,
        components: Components = this.components,
        settings: Settings = this.settings,
        lifecycleOwner: LifecycleOwner = this.lifecycleOwner,
        navController: NavController = this.navController,
    ): Pair<BrowserToolbarSearchMiddleware, BrowserToolbarStore> {
        val middleware = buildMiddleware(appStore, browserStore, components, settings)
        val store = BrowserToolbarStore(
            middleware = listOf(middleware),
        ).also {
            it.dispatch(
                EnvironmentRehydrated(
                    HomeToolbarEnvironment(
                        testContext, lifecycleOwner, navController, mockk(),
                    ),
                ),
            )
        }

        return middleware to store
    }

    private fun buildMiddleware(
        appStore: AppStore = this.appStore,
        browserStore: BrowserStore = this.browserStore,
        components: Components = this.components,
        settings: Settings = this.settings,
    ) = BrowserToolbarSearchMiddleware(appStore, browserStore, components, settings)

    private fun configureAutocompleteProvidersInComponents() {
        every { components.core.historyStorage } returns mockk()
        every { components.core.bookmarksStorage } returns mockk()
        every { components.core.domainsAutocompleteProvider } returns mockk()
        every { components.core.sessionAutocompleteProvider } returns mockk()
        every { components.backgroundServices.syncedTabsAutocompleteProvider } returns mockk()
    }

    private fun fakeSearchState() = SearchState(
        region = RegionState("US", "US"),
        regionSearchEngines = listOf(
            SearchEngine("engine-a", "Engine A", mock(), type = SearchEngine.Type.BUNDLED),
            SearchEngine("engine-b", "Engine B", mock(), type = SearchEngine.Type.BUNDLED),
        ),
        customSearchEngines = listOf(
            SearchEngine("engine-c", "Engine C", mock(), type = SearchEngine.Type.CUSTOM),
        ),
        applicationSearchEngines = listOf(
            SearchEngine(TABS_SEARCH_ENGINE_ID, "Tabs", mock(), type = SearchEngine.Type.APPLICATION),
            SearchEngine(BOOKMARKS_SEARCH_ENGINE_ID, "Bookmarks", mock(), type = SearchEngine.Type.APPLICATION),
            SearchEngine(HISTORY_SEARCH_ENGINE_ID, "History", mock(), type = SearchEngine.Type.APPLICATION),
        ),
        additionalSearchEngines = listOf(
            SearchEngine("engine-e", "Engine E", mock(), type = SearchEngine.Type.BUNDLED_ADDITIONAL),
        ),
        additionalAvailableSearchEngines = listOf(
            SearchEngine("engine-f", "Engine F", mock(), type = SearchEngine.Type.BUNDLED_ADDITIONAL),
        ),
        hiddenSearchEngines = listOf(
            SearchEngine("engine-g", "Engine G", mock(), type = SearchEngine.Type.BUNDLED),
        ),
        regionDefaultSearchEngineId = null,
        userSelectedSearchEngineId = null,
        userSelectedSearchEngineName = null,
    )
}
