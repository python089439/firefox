/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.compose.snackbar

import android.view.View
import android.view.ViewGroup
import android.widget.FrameLayout
import androidx.compose.animation.core.DecayAnimationSpec
import androidx.compose.animation.rememberSplineBasedDecay
import androidx.compose.foundation.ExperimentalFoundationApi
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.Snackbar
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.ExperimentalComposeUiApi
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.ComposeView
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.platform.LocalLayoutDirection
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.testTagsAsResourceId
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.LayoutDirection
import androidx.compose.ui.unit.dp
import androidx.constraintlayout.widget.ConstraintLayout
import androidx.coordinatorlayout.widget.CoordinatorLayout
import androidx.core.view.setPadding
import com.google.android.material.snackbar.BaseTransientBottomBar
import kotlinx.coroutines.launch
import mozilla.components.compose.base.annotation.FlexibleWindowLightDarkPreview
import mozilla.components.compose.base.button.PrimaryButton
import mozilla.components.compose.base.button.TextButton
import org.mozilla.fenix.R
import org.mozilla.fenix.components.SnackbarBehavior
import org.mozilla.fenix.compose.SwipeToDismissBox2
import org.mozilla.fenix.compose.SwipeToDismissState2
import org.mozilla.fenix.compose.core.Action
import org.mozilla.fenix.compose.snackbar.SnackbarState.Type
import org.mozilla.fenix.ext.settings
import org.mozilla.fenix.theme.FirefoxTheme
import com.google.android.material.snackbar.Snackbar as MaterialSnackbar

const val SNACKBAR_TEST_TAG = "snackbar"
const val SNACKBAR_BUTTON_TEST_TAG = "snackbar_button"

private val snackbarBottomSpacing = 8.dp
private val snackbarHorizontalMargin = 16.dp
private val snackbarHorizontalPadding = 16.dp
private val snackbarVerticalPadding = 12.dp
private val snackbarActionHorizontalSpacing = 8.dp

/**
 * A Snackbar embedded within a View. To display a Snackbar embedded in a View hierarchy, use
 * [Snackbar.make]. For rendering Snackbars within Compose, consider using [SnackbarHost] instead.
 *
 * @param content The UI of the Snackbar.
 * @param parent The parent View to embed the Snackbar in.
 * @param snackbarAnimationCallback [SnackbarAnimationCallback] used to add animations to the Snackbar.
 */
class Snackbar private constructor(
    content: ComposeView,
    parent: ViewGroup,
    snackbarAnimationCallback: SnackbarAnimationCallback,
) : BaseTransientBottomBar<Snackbar>(parent, content, snackbarAnimationCallback) {

    init {
        // Ensure the underlying background color does not show and delegate the UI
        // to [content]. Also remove any unintended padding and delegate the padding to the Composable.
        view.setBackgroundColor(android.graphics.Color.TRANSPARENT)
        view.setPadding(0)
    }

    /**
     * Snackbar helper object
     */
    companion object {
        private const val LENGTH_ACCESSIBLE = 15000 // 15 seconds in ms

        /**
         * Display a snackbar in the given View with the given [SnackbarState]. For rendering
         * Snackbars within Compose, consider using [SnackbarHost] instead.
         *
         * @param snackBarParentView The [View] to embed the Snackbar in.
         * @param snackbarState [SnackbarState] containing the data parameters of the Snackbar.
         */
        @OptIn(ExperimentalFoundationApi::class)
        fun make(
            snackBarParentView: View,
            snackbarState: SnackbarState,
        ): Snackbar {
            val parent = findSuitableParent(snackBarParentView) ?: run {
                throw IllegalArgumentException(
                    "No suitable parent found from the given view. Please provide a valid view.",
                )
            }
            val contentView = ComposeView(context = parent.context)
            val callback = SnackbarAnimationCallback(contentView)
            val durationOrAccessibleDuration =
                if (parent.context.settings().accessibilityServicesEnabled &&
                    LENGTH_ACCESSIBLE > snackbarState.durationMs
                ) {
                    LENGTH_ACCESSIBLE
                } else {
                    snackbarState.durationMs
                }

            return Snackbar(
                content = contentView,
                parent = parent,
                snackbarAnimationCallback = callback,
            ).also { snackbar ->
                val action = snackbarState.action?.let {
                    Action(
                        label = snackbarState.action.label,
                        onClick = {
                            snackbarState.action.onClick()
                            snackbar.dismiss()
                        },
                    )
                }

                contentView.setContent {
                    FirefoxTheme {
                        val density = LocalDensity.current
                        val isRtl = LocalLayoutDirection.current == LayoutDirection.Rtl
                        val decayAnimationSpec: DecayAnimationSpec<Float> = rememberSplineBasedDecay()
                        val swipeState = remember {
                            SwipeToDismissState2(
                                density = density,
                                decayAnimationSpec = decayAnimationSpec,
                                isRtl = isRtl,
                            )
                        }

                        SwipeToDismissBox2(
                            state = swipeState,
                            modifier = Modifier.fillMaxWidth(),
                            onItemDismiss = {
                                snackbarState.onDismiss.invoke()
                            },
                            backgroundContent = {},
                        ) {
                            Snackbar(
                                snackbarState = snackbarState.copy(action = action),
                            )
                        }
                    }
                }

                snackbar.duration = durationOrAccessibleDuration

                if (parent.id == R.id.dynamicSnackbarContainer) {
                    (parent.layoutParams as? CoordinatorLayout.LayoutParams)?.apply {
                        behavior = SnackbarBehavior<FrameLayout>(
                            context = snackBarParentView.context,
                            toolbarPosition = snackBarParentView.context.settings().toolbarPosition,
                            shouldUseExpandedToolbar = snackBarParentView.context.settings().shouldUseExpandedToolbar,
                        )
                    }
                }
            }
        }

        /**
         * This is a re-implementation of [MaterialSnackbar.findSuitableParent].
         */
        @Suppress("ReturnCount")
        private fun findSuitableParent(view: View?): ViewGroup? {
            var currentView = view
            var fallback: ViewGroup? = null

            do {
                /**
                 * A [ConstraintLayout] parent overcomes the issue with snackbars internally
                 * positioning themselves above the OS navigation bar or IMEs when using edge-to-edge
                 * https://github.com/material-components/material-components-android/issues/3446
                 */
                if (currentView is ConstraintLayout && currentView.id == R.id.dynamicSnackbarContainer) {
                    return currentView
                }

                if (currentView is CoordinatorLayout) {
                    return currentView
                }

                if (currentView is FrameLayout &&
                    (currentView.id == android.R.id.content || currentView.id == R.id.dynamicSnackbarContainer)
                ) {
                    return currentView
                } else if (currentView is FrameLayout) {
                    fallback = currentView
                }

                if (currentView != null) {
                    val parent = currentView.parent
                    currentView = parent as? View
                }
            } while (currentView != null)

            return fallback
        }

        private class SnackbarAnimationCallback(
            private val content: View,
        ) : com.google.android.material.snackbar.ContentViewCallback {

            override fun animateContentIn(delay: Int, duration: Int) {
                content.translationY = (content.height).toFloat()
                content.animate().apply {
                    translationY(DEFAULT_Y_TRANSLATION)
                    setDuration(ANIMATE_IN_DURATION_MS)
                    startDelay = delay.toLong()
                }
            }

            override fun animateContentOut(delay: Int, duration: Int) {
                content.translationY = DEFAULT_Y_TRANSLATION
                content.animate().apply {
                    translationY((content.height).toFloat())
                    setDuration(ANIMATE_OUT_DURATION_MS)
                    startDelay = delay.toLong()
                }
            }

            companion object {
                private const val DEFAULT_Y_TRANSLATION = 0f
                private const val ANIMATE_IN_DURATION_MS = 200L
                private const val ANIMATE_OUT_DURATION_MS = 150L
            }
        }
    }
}

/**
 * The root Snackbar UI. This is used by [Snackbar.make] and [SnackbarHost] to display Snackbar
 * style toast messages styled by the Acorn design system.
 *
 * @param snackbarState The data to display within the Snackbar.
 * @param modifier The [Modifier] used to configure the Snackbar layout.
 */
@OptIn(ExperimentalComposeUiApi::class)
@Composable
internal fun Snackbar(
    snackbarState: SnackbarState,
    modifier: Modifier = Modifier,
) {
    val colors = when (snackbarState.type) {
        Type.Default -> SnackbarColors.default
        Type.Warning -> SnackbarColors.warning
    }

    Column(
        modifier = Modifier
            .fillMaxWidth()
            .padding(horizontal = snackbarHorizontalMargin),
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        Card(
            modifier = modifier
                .widthIn(max = FirefoxTheme.layout.size.maxWidth.small)
                .semantics {
                    testTagsAsResourceId = true
                }
                .testTag(SNACKBAR_TEST_TAG),
            shape = RoundedCornerShape(size = 8.dp),
            colors = CardDefaults.cardColors(containerColor = colors.backgroundColor),
            elevation = CardDefaults.cardElevation(defaultElevation = 4.dp),
        ) {
            Row(
                modifier = Modifier.fillMaxWidth(),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                Column(
                    modifier = Modifier
                        .weight(1f)
                        .padding(
                            start = snackbarHorizontalPadding,
                            top = snackbarVerticalPadding,
                            bottom = snackbarVerticalPadding,
                        ),
                ) {
                    Text(
                        text = snackbarState.message,
                        color = colors.messageTextColor,
                        overflow = TextOverflow.Ellipsis,
                        maxLines = 2,
                        style = FirefoxTheme.typography.headline7,
                    )

                    snackbarState.subMessage?.let {
                        Text(
                            text = it,
                            color = colors.messageTextColor,
                            overflow = TextOverflow.Ellipsis,
                            maxLines = 1,
                            style = FirefoxTheme.typography.caption,
                        )
                    }
                }

                if (snackbarState.action != null) {
                    Spacer(modifier = Modifier.width(snackbarActionHorizontalSpacing))

                    TextButton(
                        text = snackbarState.action.label,
                        onClick = snackbarState.action.onClick,
                        modifier = Modifier.testTag(SNACKBAR_BUTTON_TEST_TAG),
                        textColor = colors.actionTextColor,
                    )

                    Spacer(modifier = Modifier.width(snackbarActionHorizontalSpacing))
                } else {
                    Spacer(modifier = Modifier.width(snackbarHorizontalPadding))
                }
            }
        }

        Spacer(modifier = Modifier.height(snackbarBottomSpacing))
    }
}

/**
 * The colors used to style [Snackbar].
 *
 * @property messageTextColor The [Color] applied to the Snackbar's message.
 * @property actionTextColor The [Color] applied to the Snackbar's text button.
 * @property backgroundColor The [Color] applied to the Snackbar's background.
 */
private data class SnackbarColors(
    val messageTextColor: Color,
    val actionTextColor: Color,
    val backgroundColor: Color,
) {
    companion object {
        val default: SnackbarColors
            @Composable get() = SnackbarColors(
                messageTextColor = FirefoxTheme.colors.textActionPrimary,
                actionTextColor = FirefoxTheme.colors.textActionPrimary,
                backgroundColor = FirefoxTheme.colors.actionPrimary,
            )

        val warning: SnackbarColors
            @Composable get() = SnackbarColors(
                messageTextColor = FirefoxTheme.colors.textCritical,
                actionTextColor = FirefoxTheme.colors.textPrimary,
                backgroundColor = FirefoxTheme.colors.layer3,
            )
    }
}

@FlexibleWindowLightDarkPreview
@Composable
@Suppress("LongMethod")
private fun SnackbarHostPreview() {
    val snackbarHostState = remember { AcornSnackbarHostState() }
    var defaultSnackbarClicks by remember { mutableIntStateOf(0) }
    var warningSnackbarClicks by remember { mutableIntStateOf(0) }
    val scope = rememberCoroutineScope()

    FirefoxTheme {
        Box(
            modifier = Modifier
                .fillMaxSize()
                .background(color = FirefoxTheme.colors.layer1)
                .padding(all = 16.dp),
        ) {
            Column {
                PrimaryButton(
                    text = "Show snackbar",
                    modifier = Modifier.fillMaxWidth(),
                ) {
                    scope.launch {
                        snackbarHostState.showSnackbar(
                            snackbarState = SnackbarState(
                                message = "Default snackbar",
                                subMessage = "Default subMessage",
                                duration = SnackbarState.Duration.Preset.Short,
                                type = Type.Default,
                                action = Action(
                                    label = "click me",
                                    onClick = {
                                        defaultSnackbarClicks++
                                    },
                                ),
                                onDismiss = {},
                            ),
                        )
                    }
                }

                Spacer(modifier = Modifier.height(16.dp))

                PrimaryButton(
                    text = "Show warning snackbar",
                    modifier = Modifier.fillMaxWidth(),
                ) {
                    scope.launch {
                        snackbarHostState.showSnackbar(
                            snackbarState = SnackbarState(
                                message = "Warning snackbar",
                                subMessage = "Default subMessage",
                                duration = SnackbarState.Duration.Preset.Short,
                                type = Type.Warning,
                                action = Action(
                                    label = "click me",
                                    onClick = {
                                        warningSnackbarClicks++
                                    },
                                ),
                                onDismiss = {},
                            ),
                        )
                    }
                }

                Spacer(modifier = Modifier.height(16.dp))

                Text(
                    text = "Default snackbar action clicks: $defaultSnackbarClicks",
                    color = FirefoxTheme.colors.textPrimary,
                )

                Spacer(modifier = Modifier.height(16.dp))

                Text(
                    text = "Warning snackbar action clicks: $warningSnackbarClicks",
                    color = FirefoxTheme.colors.textPrimary,
                )

                Spacer(modifier = Modifier.height(16.dp))
            }

            SnackbarHost(
                snackbarHostState = snackbarHostState,
                modifier = Modifier.align(Alignment.BottomCenter),
            )
        }
    }
}

@FlexibleWindowLightDarkPreview
@Composable
private fun SnackbarPreview() {
    FirefoxTheme {
        Snackbar(
            SnackbarState(
                message = "Regular snackbar",
            ),
        )
    }
}

@FlexibleWindowLightDarkPreview
@Composable
private fun LongSnackbarPreview() {
    FirefoxTheme {
        Snackbar(
            SnackbarState(
                message = "Regular snackbar with a very very long wrapping message",
            ),
        )
    }
}

@FlexibleWindowLightDarkPreview
@Composable
private fun SnackbarActionPreview() {
    FirefoxTheme {
        Snackbar(
            SnackbarState(
                message = "Regular snackbar",
                action = Action(
                    label = "Click me",
                    onClick = {},
                ),
            ),
        )
    }
}

@FlexibleWindowLightDarkPreview
@Composable
private fun LongSnackbarActionPreview() {
    FirefoxTheme {
        Snackbar(
            SnackbarState(
                message = "Regular snackbar with a very very long wrapping message",
                action = Action(
                    label = "Click me",
                    onClick = {},
                ),
            ),
        )
    }
}

@FlexibleWindowLightDarkPreview
@Composable
private fun WarningSnackbarPreview() {
    FirefoxTheme {
        Snackbar(
            SnackbarState(
                message = "Warning snackbar",
                type = Type.Warning,
            ),
        )
    }
}

@FlexibleWindowLightDarkPreview
@Composable
private fun WarningSnackbarActionPreview() {
    FirefoxTheme {
        Snackbar(
            SnackbarState(
                message = "Warning snackbar",
                type = Type.Warning,
                action = Action(
                    label = "Click me",
                    onClick = {},
                ),
            ),
        )
    }
}
