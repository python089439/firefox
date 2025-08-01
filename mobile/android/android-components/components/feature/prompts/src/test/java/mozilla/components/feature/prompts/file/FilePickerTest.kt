/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.prompts.file

import android.Manifest
import android.app.Activity.RESULT_CANCELED
import android.app.Activity.RESULT_OK
import android.content.ClipData
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager.PERMISSION_DENIED
import android.content.pm.PackageManager.PERMISSION_GRANTED
import android.net.Uri
import androidx.activity.result.contract.ActivityResultContracts
import androidx.core.net.toUri
import androidx.test.core.app.ApplicationProvider
import androidx.test.ext.junit.runners.AndroidJUnit4
import mozilla.components.browser.state.action.ContentAction
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.browser.state.state.ContentState
import mozilla.components.browser.state.state.CustomTabSessionState
import mozilla.components.browser.state.state.TabSessionState
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.concept.engine.prompt.PromptRequest
import mozilla.components.feature.prompts.PromptContainer
import mozilla.components.feature.prompts.file.FilePicker.Companion.FILE_PICKER_ACTIVITY_REQUEST_CODE
import mozilla.components.feature.prompts.file.FilePicker.Companion.FOLDER_PICKER_ACTIVITY_REQUEST_CODE
import mozilla.components.support.test.any
import mozilla.components.support.test.eq
import mozilla.components.support.test.mock
import mozilla.components.support.test.robolectric.grantPermission
import mozilla.components.support.test.robolectric.testContext
import mozilla.components.support.test.whenever
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.mockito.Mockito.anyInt
import org.mockito.Mockito.doNothing
import org.mockito.Mockito.doReturn
import org.mockito.Mockito.never
import org.mockito.Mockito.spy
import org.mockito.Mockito.verify
import org.mockito.Mockito.verifyNoInteractions
import org.robolectric.annotation.Config
import java.io.File

@RunWith(AndroidJUnit4::class)
class FilePickerTest {

    private val noopSingle: (Context, Uri) -> Unit = { _, _ -> }
    private val noopMulti: (Context, Array<Uri>) -> Unit = { _, _ -> }
    private val request = PromptRequest.File(
        mimeTypes = emptyArray(),
        onSingleFileSelected = noopSingle,
        onMultipleFilesSelected = noopMulti,
        onDismiss = {},
    )
    private val requestFolder = PromptRequest.Folder(
        onSelected = noopSingle,
        onDismiss = {},
    )

    private lateinit var fragment: PromptContainer
    private lateinit var store: BrowserStore
    private lateinit var state: BrowserState
    private lateinit var filePicker: FilePicker
    private lateinit var fileUploadsDirCleaner: FileUploadsDirCleaner

    @Before
    fun setup() {
        fileUploadsDirCleaner = mock()
        fragment = spy(PromptContainer.TestPromptContainer(testContext))
        state = mock()
        store = mock()
        whenever(store.state).thenReturn(state)
        filePicker = FilePicker(
            fragment,
            store,
            fileUploadsDirCleaner = fileUploadsDirCleaner,
        ) { }
    }

    @Test
    fun `FilePicker acts on a given (custom tab) session or the selected session`() {
        val customTabContent: ContentState = mock()
        whenever(customTabContent.promptRequests).thenReturn(listOf(request))
        val customTab = CustomTabSessionState(id = "custom-tab", content = customTabContent, trackingProtection = mock(), config = mock())

        whenever(state.customTabs).thenReturn(listOf(customTab))
        filePicker = FilePicker(
            fragment,
            store,
            customTab.id,
            fileUploadsDirCleaner = mock(),
        ) { }
        filePicker.onActivityResult(FILE_PICKER_ACTIVITY_REQUEST_CODE, 0, null)
        verify(store).dispatch(ContentAction.ConsumePromptRequestAction(customTab.id, request))

        val selected = prepareSelectedSession(request)
        filePicker = FilePicker(
            fragment,
            store,
            fileUploadsDirCleaner = mock(),
        ) { }
        filePicker.onActivityResult(FILE_PICKER_ACTIVITY_REQUEST_CODE, 0, null)
        verify(store).dispatch(ContentAction.ConsumePromptRequestAction(selected.id, request))
    }

    @Test
    @Config(sdk = [32])
    fun `handleFilePickerRequest without the required permission will call askAndroidPermissionsForRequest on SDK 32 and below`() {
        var onRequestPermissionWasCalled = false
        val context = ApplicationProvider.getApplicationContext<Context>()

        filePicker = spy(
            FilePicker(
                fragment,
                store,
                fileUploadsDirCleaner = mock(),
            ) {
                onRequestPermissionWasCalled = true
            },
        )

        doReturn(context).`when`(fragment).context

        filePicker.handleFileRequest(request)

        assertTrue(onRequestPermissionWasCalled)
        verify(filePicker).askAndroidPermissionsForRequest(any(), eq(request))
        verify(fragment, never()).startActivityForResult(Intent(), 1)
    }

    @Test
    @Config(sdk = [28])
    fun `handleFilePickerRequest with the required permission will call startActivityForResult on SDK 28`() {
        var onRequestPermissionWasCalled = false

        filePicker = FilePicker(
            fragment,
            store,
            fileUploadsDirCleaner = mock(),
        ) {
            onRequestPermissionWasCalled = true
        }

        grantPermission(Manifest.permission.READ_EXTERNAL_STORAGE)

        filePicker.handleFileRequest(request)

        assertFalse(onRequestPermissionWasCalled)
        verify(fragment).startActivityForResult(any(), anyInt())
    }

    @Test
    fun `handleFilePickerRequest with the required permission will call startActivityForResult`() {
        var onRequestPermissionWasCalled = false

        filePicker = FilePicker(
            fragment,
            store,
            fileUploadsDirCleaner = mock(),
        ) {
            onRequestPermissionWasCalled = true
        }

        grantPermission(
            Manifest.permission.READ_MEDIA_AUDIO,
        )

        filePicker.handleFileRequest(request)

        assertFalse(onRequestPermissionWasCalled)
        verify(fragment).startActivityForResult(any(), anyInt())
    }

    @Test
    fun `onPermissionsGranted will forward call to to build intents and show file chooser`() {
        stubContext()
        filePicker = spy(filePicker)
        filePicker.currentRequest = request

        filePicker.onPermissionsGranted(request)

        // The original prompt that started the request permission flow is persisted in the store
        // That should not be accesses / modified in any way.
        verifyNoInteractions(store)
        // After the permission is granted we should retry picking a file based on the original request.
        verify(filePicker).buildIntentList(eq(request))
        verify(filePicker).showChooser(any())
    }

    @Test
    fun `onPermissionsDeny will call onDismiss and consume the file PromptRequest of the actual session if androidPhotoPicker is null`() {
        var onDismissWasCalled = false
        val filePickerRequest = request.copy {
            onDismissWasCalled = true
        }

        val selected = prepareSelectedSession(filePickerRequest)

        stubContext()

        filePicker.onPermissionsDenied()

        assertTrue(onDismissWasCalled)
        verify(store).dispatch(ContentAction.ConsumePromptRequestAction(selected.id, filePickerRequest))
    }

    @Test
    fun `onActivityResult with RESULT_OK and isMultipleFilesSelection false will consume PromptRequest of the actual session`() {
        var onSingleFileSelectionWasCalled = false

        val onSingleFileSelection: (Context, Uri) -> Unit = { _, _ ->
            onSingleFileSelectionWasCalled = true
        }

        val filePickerRequest = request.copy(onSingleFileSelected = onSingleFileSelection)

        val selected = prepareSelectedSession(filePickerRequest)
        val intent = Intent()

        intent.data = mock()

        stubContext()

        filePicker.onActivityResult(FILE_PICKER_ACTIVITY_REQUEST_CODE, RESULT_OK, intent)

        assertTrue(onSingleFileSelectionWasCalled)
        verify(store).dispatch(ContentAction.ConsumePromptRequestAction(selected.id, filePickerRequest))
    }

    @Test
    fun `onActivityResult with RESULT_OK and isMultipleFilesSelection true will consume PromptRequest of the actual session`() {
        var onMultipleFileSelectionWasCalled = false

        val onMultipleFileSelection: (Context, Array<Uri>) -> Unit = { _, _ ->
            onMultipleFileSelectionWasCalled = true
        }

        val filePickerRequest = request.copy(
            isMultipleFilesSelection = true,
            onMultipleFilesSelected = onMultipleFileSelection,
        )

        val selected = prepareSelectedSession(filePickerRequest)
        val intent = Intent()

        intent.clipData = mock()
        val item = mock<ClipData.Item>()

        doReturn(mock<Uri>()).`when`(item).uri

        intent.clipData?.apply {
            doReturn(1).`when`(this).itemCount
            doReturn(item).`when`(this).getItemAt(0)
        }

        stubContext()

        filePicker.onActivityResult(FILE_PICKER_ACTIVITY_REQUEST_CODE, RESULT_OK, intent)

        assertTrue(onMultipleFileSelectionWasCalled)
        verify(store).dispatch(ContentAction.ConsumePromptRequestAction(selected.id, filePickerRequest))
    }

    @Test
    fun `onActivityResult with not RESULT_OK will consume PromptRequest of the actual session and call onDismiss `() {
        var onDismissWasCalled = false

        val filePickerRequest = request.copy(isMultipleFilesSelection = true) {
            onDismissWasCalled = true
        }

        val selected = prepareSelectedSession(filePickerRequest)
        val intent = Intent()

        filePicker.onActivityResult(FILE_PICKER_ACTIVITY_REQUEST_CODE, RESULT_CANCELED, intent)

        assertTrue(onDismissWasCalled)
        verify(store).dispatch(ContentAction.ConsumePromptRequestAction(selected.id, filePickerRequest))
    }

    @Test
    fun `onActivityResult will not process any PromptRequest that is not a File request`() {
        var wasConfirmed = false
        var wasDismissed = false
        val onConfirm: (Boolean) -> Unit = { wasConfirmed = true }
        val onDismiss = { wasDismissed = true }
        val invalidRequest = PromptRequest.Alert("", "", false, onConfirm, onDismiss)
        val spiedFilePicker = spy(filePicker)
        val selected = prepareSelectedSession(invalidRequest)
        val intent = Intent()

        spiedFilePicker.onActivityResult(FILE_PICKER_ACTIVITY_REQUEST_CODE, RESULT_OK, intent)

        assertFalse(wasConfirmed)
        assertFalse(wasDismissed)
        verify(store, never()).dispatch(ContentAction.ConsumePromptRequestAction(selected.id, request))
        verify(spiedFilePicker, never()).handleFilePickerIntentResult(intent, request)
    }

    @Test
    fun `onActivityResult returns false if the request code is not the same`() {
        val intent = Intent()
        val result = filePicker.onActivityResult(10101, RESULT_OK, intent)

        assertFalse(result)
    }

    @Test
    fun `onRequestPermissionsResult with FILE_PICKER_REQUEST and PERMISSION_GRANTED will call onPermissionsGranted`() {
        stubContext()
        filePicker = spy(filePicker)
        filePicker.currentRequest = request

        filePicker.onPermissionsResult(emptyArray(), IntArray(1) { PERMISSION_GRANTED })

        verify(filePicker).onPermissionsGranted(request)
    }

    @Test
    fun `onRequestPermissionsResult with FILE_PICKER_REQUEST and PERMISSION_DENIED will call onPermissionsDeny`() {
        filePicker = spy(filePicker)
        doNothing().`when`(filePicker).onPermissionsDenied()

        filePicker.onPermissionsResult(emptyArray(), IntArray(1) { PERMISSION_DENIED })

        verify(filePicker).onPermissionsDenied()
    }

    @Test
    fun `askAndroidPermissionsForRequest should cache the current request and then ask for permissions`() {
        val permissions = setOf("PermissionA")
        var permissionsRequested = emptyArray<String>()
        filePicker = spy(
            FilePicker(
                fragment,
                store,
                null,
                fileUploadsDirCleaner = mock(),
            ) { requested ->
                permissionsRequested = requested
            },
        )

        filePicker.askAndroidPermissionsForRequest(permissions, request)

        assertEquals(request, filePicker.currentRequest)
        assertArrayEquals(permissions.toTypedArray(), permissionsRequested)
    }

    @Test
    fun `handleFilePickerIntentResult called with null Intent will make captureUri null`() {
        stubContext()
        captureUri = "randomSaveLocationOnDisk".toUri()
        val promptRequest = mock<PromptRequest.File>()
        doReturn(noopSingle).`when`(promptRequest).onSingleFileSelected

        filePicker.handleFilePickerIntentResult(null, promptRequest)

        assertNull(captureUri)
    }

    @Test
    @Config(sdk = [29])
    fun `handleFilePickerIntentResult called with valid Intent will make captureUri null also if request is dismissed on SDK 29 and below`() {
        stubContext()
        captureUri = "randomSaveLocationOnDisk".toUri()
        val promptRequest = mock<PromptRequest.File>()
        doReturn({ }).`when`(promptRequest).onDismiss
        // A private file cannot be picked so the request will be dismissed.
        val intent = Intent().apply {
            data = ("file://" + File(testContext.applicationInfo.dataDir, "randomFile").canonicalPath).toUri()
        }

        filePicker.handleFilePickerIntentResult(intent, promptRequest)

        assertNull(captureUri)
    }

    @Test
    fun `handleFilePickerIntentResult called with valid Intent will make captureUri null also if request is dismissed`() {
        stubContext()
        captureUri = "randomSaveLocationOnDisk".toUri()
        val promptRequest = mock<PromptRequest.File>()
        doReturn({ }).`when`(promptRequest).onDismiss
        doReturn(noopSingle).`when`(promptRequest).onSingleFileSelected
        // A private file cannot be picked so the request will be dismissed.
        val intent = Intent().apply {
            data = ("file://" + File(testContext.applicationInfo.dataDir, "randomFile").canonicalPath).toUri()
        }

        filePicker.handleFilePickerIntentResult(intent, promptRequest)

        assertNull(captureUri)
    }

    @Test
    fun `handleFilePickerIntentResult for multiple files selection will make captureUri null`() {
        stubContext()
        captureUri = "randomSaveLocationOnDisk".toUri()
        val promptRequest = mock<PromptRequest.File>()
        doReturn(noopMulti).`when`(promptRequest).onMultipleFilesSelected
        doReturn(true).`when`(promptRequest).isMultipleFilesSelection
        val intent = Intent().apply {
            clipData = (ClipData.newRawUri("Test", "https://www.mozilla.org".toUri()))
        }

        filePicker.handleFilePickerIntentResult(intent, promptRequest)

        verify(fileUploadsDirCleaner).enqueueForCleanup(any())
        assertNull(captureUri)
    }

    @Test
    @Config(sdk = [29])
    fun `handleFilePickerIntentResult for multiple files selection will make captureUri null also if request is dismissed on SDK 29 and below`() {
        stubContext()
        captureUri = "randomSaveLocationOnDisk".toUri()
        val promptRequest = mock<PromptRequest.File>()
        doReturn({ }).`when`(promptRequest).onDismiss
        doReturn(true).`when`(promptRequest).isMultipleFilesSelection
        // A private file cannot be picked so the request will be dismissed.
        val intent = Intent().apply {
            clipData = (
                ClipData.newRawUri(
                    "Test",
                    ("file://" + File(testContext.applicationInfo.dataDir, "randomFile").canonicalPath).toUri(),
                )
                )
        }

        filePicker.handleFilePickerIntentResult(intent, promptRequest)

        assertNull(captureUri)
    }

    @Test
    fun `handleFilePickerIntentResult for multiple files selection will make captureUri null also if request is dismissed`() {
        stubContext()
        captureUri = "randomSaveLocationOnDisk".toUri()
        val promptRequest = mock<PromptRequest.File>()
        doReturn({ }).`when`(promptRequest).onDismiss
        doReturn(true).`when`(promptRequest).isMultipleFilesSelection
        doReturn(noopMulti).`when`(promptRequest).onMultipleFilesSelected

        // A private file cannot be picked so the request will be dismissed.
        val intent = Intent().apply {
            clipData = (
                ClipData.newRawUri(
                    "Test",
                    ("file://" + File(testContext.applicationInfo.dataDir, "randomFile").canonicalPath).toUri(),
                )
                )
        }

        filePicker.handleFilePickerIntentResult(intent, promptRequest)

        assertNull(captureUri)
    }

    @Test
    fun `canUseAndroidPhotoPicker returns true when conditions are met`() {
        val mockAndroidPhotoPicker = mock<AndroidPhotoPicker>()

        val filePickerSpy = spy(filePicker)
        filePickerSpy.currentRequest = request
        filePickerSpy.androidPhotoPicker = mockAndroidPhotoPicker

        whenever(filePickerSpy.isPhotoOrVideoRequest(request)).thenReturn(true)
        whenever(mockAndroidPhotoPicker.isPhotoPickerAvailable).thenReturn(true)

        val result = filePickerSpy.canUseAndroidPhotoPicker()

        assertTrue(result)
    }

    @Test
    fun `canUseAndroidPhotoPicker returns false when the request is not for photo or video`() {
        val mockAndroidPhotoPicker = mock<AndroidPhotoPicker>()

        val filePickerSpy = spy(filePicker)
        filePickerSpy.currentRequest = request
        filePickerSpy.androidPhotoPicker = mockAndroidPhotoPicker

        whenever(filePickerSpy.isPhotoOrVideoRequest(request)).thenReturn(false)
        whenever(mockAndroidPhotoPicker.isPhotoPickerAvailable).thenReturn(true)

        val result = filePickerSpy.canUseAndroidPhotoPicker()

        assertFalse(result)
    }

    @Test
    fun `canUseAndroidPhotoPicker returns false when photo picker is not available`() {
        val mockAndroidPhotoPicker = mock<AndroidPhotoPicker>()

        val filePickerSpy = spy(filePicker)
        filePickerSpy.currentRequest = request
        filePickerSpy.androidPhotoPicker = mockAndroidPhotoPicker

        whenever(filePickerSpy.isPhotoOrVideoRequest(request)).thenReturn(true)
        whenever(mockAndroidPhotoPicker.isPhotoPickerAvailable).thenReturn(false)

        val result = filePickerSpy.canUseAndroidPhotoPicker()

        assertFalse(result)
    }

    @Test
    fun `canUseAndroidPhotoPicker returns false when androidPhotoPicker is null`() {
        val mockAndroidPhotoPicker = mock<AndroidPhotoPicker>()

        val filePickerSpy = spy(filePicker)
        filePickerSpy.currentRequest = request
        filePickerSpy.androidPhotoPicker = null

        whenever(filePickerSpy.isPhotoOrVideoRequest(request)).thenReturn(true)
        whenever(mockAndroidPhotoPicker.isPhotoPickerAvailable).thenReturn(false)

        val result = filePickerSpy.canUseAndroidPhotoPicker()

        assertFalse(result)
    }

    @Test
    fun `isPhotoOrVideoRequest returns true for image and video mime types`() {
        val request = PromptRequest.File(
            arrayOf("image/png", "video/mp4"),
            onSingleFileSelected = noopSingle,
            onMultipleFilesSelected = noopMulti,
            onDismiss = {},
        )

        val filePickerSpy = spy(filePicker)

        val result = filePickerSpy.isPhotoOrVideoRequest(request)

        assertTrue(result)
    }

    @Test
    fun `isPhotoOrVideoRequest returns false for non-image and non-video mime types`() {
        val request = PromptRequest.File(
            arrayOf("application/pdf"),
            onSingleFileSelected = noopSingle,
            onMultipleFilesSelected = noopMulti,
            onDismiss = {},
        )

        val filePickerSpy = spy(filePicker)

        val result = filePickerSpy.isPhotoOrVideoRequest(request)

        assertFalse(result)
    }

    @Test
    fun `getVisualMediaType returns SingleMimeType when mime types contain only one mime type`() {
        val request = PromptRequest.File(
            arrayOf("image/png"),
            onSingleFileSelected = { _, _ -> },
            onMultipleFilesSelected = { _, _ -> },
            onDismiss = {},
        )

        val result = filePicker.getVisualMediaType(request)

        assertTrue(result is ActivityResultContracts.PickVisualMedia.SingleMimeType)
        assertEquals(
            "image/png",
            (result as ActivityResultContracts.PickVisualMedia.SingleMimeType).mimeType,
        )
    }

    @Test
    fun `getVisualMediaType returns ImageAndVideo when mime types contain both image and video`() {
        val request = PromptRequest.File(
            arrayOf("image/png", "video/mp4"),
            onSingleFileSelected = { _, _ -> },
            onMultipleFilesSelected = { _, _ -> },
            onDismiss = {},
        )

        val result = filePicker.getVisualMediaType(request)

        assertEquals(
            ActivityResultContracts.PickVisualMedia.ImageAndVideo,
            result,
        )
    }

    @Test
    fun `getVisualMediaType returns ImageOnly when mime types contain only image`() {
        val request = PromptRequest.File(
            arrayOf("image/png", "image/jpeg"),
            onSingleFileSelected = { _, _ -> },
            onMultipleFilesSelected = { _, _ -> },
            onDismiss = {},
        )

        val result = filePicker.getVisualMediaType(request)

        assertEquals(ActivityResultContracts.PickVisualMedia.ImageOnly, result)
    }

    @Test
    fun `getVisualMediaType returns VideoOnly when mime types contain only video`() {
        val request = PromptRequest.File(
            arrayOf("video/mp4", "video/avi"),
            onSingleFileSelected = { _, _ -> },
            onMultipleFilesSelected = { _, _ -> },
            onDismiss = {},
        )

        val result = filePicker.getVisualMediaType(request)

        assertEquals(ActivityResultContracts.PickVisualMedia.VideoOnly, result)
    }

    @Test(expected = IllegalStateException::class)
    fun `getVisualMediaType throws IllegalStateException when mime types do not contain image or video`() {
        val request = PromptRequest.File(
            arrayOf("application/pdf"),
            onSingleFileSelected = { _, _ -> },
            onMultipleFilesSelected = { _, _ -> },
            onDismiss = {},
        )

        filePicker.getVisualMediaType(request)
    }

    @Test
    fun `onActivityResult for folder with RESULT_OK consume PromptRequest of the current session`() {
        var onFolderSelectionWasCalled = false

        val onFolderSelection: (Context, Uri) -> Unit = { _, _ ->
            onFolderSelectionWasCalled = true
        }

        val filePickerRequest = requestFolder.copy(onSelected = onFolderSelection)

        val selected = prepareSelectedSession(filePickerRequest)
        val intent = Intent()

        intent.data = mock()

        stubContext()

        filePicker.onActivityResult(FOLDER_PICKER_ACTIVITY_REQUEST_CODE, RESULT_OK, intent)

        assertTrue(onFolderSelectionWasCalled)
        verify(store).dispatch(ContentAction.ConsumePromptRequestAction(selected.id, filePickerRequest))
    }

    @Test
    fun `onActivityResult for folder with not RESULT_OK ill consume PromptRequest of the actual session and call onDismiss `() {
        var onDismissWasCalled = false

        val filePickerRequest = requestFolder.copy {
            onDismissWasCalled = true
        }

        val selected = prepareSelectedSession(filePickerRequest)
        val intent = Intent()

        filePicker.onActivityResult(FOLDER_PICKER_ACTIVITY_REQUEST_CODE, RESULT_CANCELED, intent)

        assertTrue(onDismissWasCalled)
        verify(store).dispatch(ContentAction.ConsumePromptRequestAction(selected.id, filePickerRequest))
    }

    private fun prepareSelectedSession(request: PromptRequest? = null): TabSessionState {
        val promptRequest: PromptRequest = request ?: mock()
        val content: ContentState = mock()
        whenever(content.promptRequests).thenReturn(listOf(promptRequest))

        val selected = TabSessionState("browser-tab", content, mock(), mock())
        whenever(state.selectedTabId).thenReturn(selected.id)
        whenever(state.tabs).thenReturn(listOf(selected))
        return selected
    }

    private fun stubContext() {
        val context = ApplicationProvider.getApplicationContext<Context>()
        doReturn(context).`when`(fragment).context
        filePicker = FilePicker(
            fragment,
            store,
            fileUploadsDirCleaner = fileUploadsDirCleaner,
        ) {}
    }
}
