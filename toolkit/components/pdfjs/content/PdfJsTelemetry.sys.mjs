/* Copyright 2013 Mozilla Foundation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

export class PdfJsTelemetryContent {
  static onViewerIsUsed() {
    Glean.pdfjs.used.add(1);
    // GLAM EXPERIMENT
    // This metric is temporary, disabled by default, and will be enabled only
    // for the purpose of experimenting with client-side sampling of data for
    // GLAM use. See Bug 1947604 for more information.
    Glean.glamExperiment.used.add(1);
  }
}

export class PdfJsTelemetry {
  static #fixKeys(data) {
    for (const [key, values] of Object.entries(data)) {
      const newKey = key.replaceAll(/([A-Z])/g, c => `_${c.toLowerCase()}`);
      if (newKey === key) {
        continue;
      }
      data[newKey] = values;
      delete data[key];
    }
    return data;
  }

  static report(aData) {
    const { type } = aData;
    switch (type) {
      case "pageInfo":
        this.onTimeToView(aData.timestamp);
        break;
      case "editing":
        this.onEditing(aData);
        break;
      case "buttons":
      case "gv-buttons":
        {
          const id = aData.data.id.replace(
            /([A-Z])/g,
            c => `_${c.toLowerCase()}`
          );
          if (type === "buttons") {
            this.onButtons(id);
          } else {
            this.onGeckoview(id);
          }
        }
        break;
      case "signatureCertificates":
        this.onSignatureCertificates(aData.data);
        break;
    }
  }

  static onSignatureCertificates(data) {
    if (!(data?.length > 0)) {
      return;
    }
    const labels = new Set([
      "adbe_x509_rsa_sha1",
      "adbe_pkcs7_detached",
      "adbe_pkcs7_sha1",
      "etsi_cades_detached",
      "etsi_rfc3161",
    ]);
    for (const cert of data) {
      const label = cert.toLowerCase().replaceAll(".", "_");
      if (labels.has(label)) {
        Glean.pdfjsDigitalSignature.certificate[label].add(1);
      } else {
        Glean.pdfjsDigitalSignature.certificate.unsupported.add(1);
      }
    }
  }

  static onTimeToView(ms) {
    Glean.pdfjs.timeToView.accumulateSingleSample(ms);
  }

  static onEditing({ type, data }) {
    if (type !== "editing" || !data) {
      return;
    }

    if (!data.type && data.action?.startsWith("pdfjs.image")) {
      this.onImage(data);
      return;
    }

    switch (data.type) {
      case "freetext":
      case "ink":
        Glean.pdfjs.editing[data.type].add(1);
        return;
      case "print":
      case "save": {
        Glean.pdfjs.editing[data.type].add(1);
        const { stats } = data;
        if (!stats) {
          return;
        }
        if (data.type === "highlight") {
          const numbers = ["one", "two", "three", "four", "five"];
          Glean.pdfjsEditingHighlight[data.type].add(1);
          Glean.pdfjsEditingHighlight.numberOfColors[
            numbers[stats.highlight.numberOfColors - 1]
          ].add(1);
          return;
        }
        if (stats.stamp) {
          this.onImage({
            action: "pdfjs.image.added",
            data: stats.stamp,
          });
        } else if (stats.signature) {
          this.onSignature({
            action: "pdfjs.signature.added",
            data: stats.signature,
          });
        }
        return;
      }
      case "signature":
        if (data.action === "added") {
          Glean.pdfjs.editing.signature.add(1);
          return;
        }

        this.onSignature(data);
        return;
      case "stamp":
        if (data.action.startsWith("pdfjs.image.")) {
          this.onImage(data);
          return;
        }

        if (data.action === "added") {
          Glean.pdfjs.editing.stamp.add(1);
          return;
        }
        Glean.pdfjs.stamp[data.action].add(1);
        for (const key of [
          "alt_text_keyboard",
          "alt_text_decorative",
          "alt_text_description",
          "alt_text_edit",
        ]) {
          if (data[key]) {
            Glean.pdfjs.stamp[key].add(1);
          }
        }
        return;
      case "highlight":
      case "free_highlight":
        switch (data.action) {
          case "added":
            Glean.pdfjsEditingHighlight.kind[data.type].add(1);
            Glean.pdfjsEditingHighlight.method[data.methodOfCreation].add(1);
            Glean.pdfjsEditingHighlight.color[data.color].add(1);
            if (data.type === "free_highlight") {
              Glean.pdfjsEditingHighlight.thickness.accumulateSingleSample(
                data.thickness
              );
            }
            break;
          case "color_changed":
            Glean.pdfjsEditingHighlight.color[data.color].add(1);
            Glean.pdfjsEditingHighlight.colorChanged.add(1);
            break;
          case "thickness_changed":
            Glean.pdfjsEditingHighlight.thickness.accumulateSingleSample(
              data.thickness
            );
            Glean.pdfjsEditingHighlight.thicknessChanged.add(1);
            break;
          case "deleted":
            Glean.pdfjsEditingHighlight.deleted.add(1);
            break;
          case "edited":
            Glean.pdfjsEditingHighlight.edited.add(1);
            break;
          case "toggle_visibility":
            Glean.pdfjsEditingHighlight.toggleVisibility.add(1);
            break;
        }
        break;
    }
  }

  static onImage({ action, data = {} }) {
    action = action.split(".").pop();

    switch (action) {
      // New alt text telemetry.
      case "info":
        Glean.pdfjsImageAltText.info.record(data);
        break;
      case "ai_generation_check":
        Glean.pdfjsImageAltText.aiGenerationCheck.record(data);
        break;
      case "settings_displayed":
        Glean.pdfjsImageAltText.settingsDisplayed.record(data);
        break;
      case "settings_ai_generation_check":
        Glean.pdfjsImageAltText.settingsAiGenerationCheck.record(data);
        break;
      case "settings_edit_alt_text_check":
        Glean.pdfjsImageAltText.settingsEditAltTextCheck.record(data);
        break;
      case "save":
        Glean.pdfjsImageAltText.save.record(data);
        break;
      case "dismiss":
        Glean.pdfjsImageAltText.dismiss.record(data);
        break;
      case "model_download_start":
        Glean.pdfjsImageAltText.modelDownloadStart.record(data);
        break;
      case "model_download_complete":
        Glean.pdfjsImageAltText.modelDownloadComplete.record(data);
        break;
      case "model_download_error":
        Glean.pdfjsImageAltText.modelDownloadError.record(data);
        break;
      case "model_deleted":
        Glean.pdfjsImageAltText.modelDeleted.record(data);
        break;
      case "model_result":
        Glean.pdfjsImageAltText.modelResult.record(data);
        break;
      case "user_edit":
        Glean.pdfjsImageAltText.userEdit.record(data);
        break;
      case "image_status_label_displayed":
        Glean.pdfjsImageAltText.imageStatusLabelDisplayed.record(data);
        break;
      case "image_status_label_clicked":
        Glean.pdfjsImageAltText.imageStatusLabelClicked.record(data);
        break;
      // Image telemetry.
      case "icon_click":
        Glean.pdfjsImage.iconClick.record(data);
        break;
      case "add_image_click":
        Glean.pdfjsImage.addImageClick.record(data);
        break;
      case "image_selected":
        Glean.pdfjsImage.imageSelected.record(data);
        break;
      case "image_added":
        Glean.pdfjsImage.imageAdded.record(data);
        break;
      case "added": {
        const { hasAltText, hasNoAltText } = data;
        Glean.pdfjsImage.added.with_alt_text.add(hasAltText);
        Glean.pdfjsImage.added.with_no_alt_text.add(hasNoAltText);
        break;
      }
      case "alt_text_edit":
        Glean.pdfjsImage.altTextEdit.ask_to_edit.set(data.ask_to_edit);
        Glean.pdfjsImage.altTextEdit.ai_generation.set(data.ai_generation);
        break;
    }
  }

  static onSignature({ action, data = {} }) {
    action = action.split(".").pop();
    data = this.#fixKeys(data);

    switch (action) {
      case "clear":
        Glean.pdfjsSignature.clear[data.type].add(1);
        break;
      case "delete_saved":
        Glean.pdfjsSignature.deleteSaved.record(data);
        break;
      case "created":
        Glean.pdfjsSignature.created.record(data);
        break;
      case "added":
        Glean.pdfjsSignature.added.record(data);
        break;
      case "inserted":
        Glean.pdfjsSignature.inserted.record(data);
        break;
      case "edit_description":
        Glean.pdfjsSignature.editDescription[
          data.has_been_changed ? "saved" : "unsaved"
        ].add(1);
        break;
    }
  }

  static onButtons(id) {
    Glean.pdfjs.buttons[id].add(1);
  }

  static onGeckoview(id) {
    Glean.pdfjs.geckoview[id].add(1);
  }
}
