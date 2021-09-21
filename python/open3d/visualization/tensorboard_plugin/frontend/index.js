// ----------------------------------------------------------------------------
// -                        Open3D: www.open3d.org                            -
// ----------------------------------------------------------------------------
// The MIT License (MIT)
//
// Copyright (c) 2018-2021 www.open3d.org
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
// IN THE SOFTWARE.
// ----------------------------------------------------------------------------

import "./adapter.min.js";
import "./webrtcstreamer.js";

class TensorboardOpen3DPluginClient {

  URL_ROUTE_PREFIX = "/data/plugin/Open3D";
  webRtcOptions = "rtptransport=tcp&timeout=60";
  width = 640;
  height = 480;
  full_width = 1280;   // video stream width, height
  full_height = 960;
  messageId = 0;
  webRtcClientList = new Map(); // {windowUId -> webRtcStreamer}
  windowState = new Map();     // {windowUId -> Geometry state (run, tags, batch_idx, step)}
  runWindow = new Map();  // {run -> windowUId}
  selectedTags = new Set();
  commonStep = null;
  commonBatchIdx = null;

  /**
   * Entry point for the TensorBoard Open3D plugin client
   * @constructor
   */
  constructor() {
    const DASHBOARD_HTML =
      `<link  href="style.css" rel="stylesheet">

            <div id="open3d-dashboard">
                <div id="options-selector">
                    <h3>Options</h3>
                    <label class="container">Sync view
                        <input type="checkbox" id="ui-options-view">
                        <span class="checkmark"></span>
                    </label>
                    <label class="container">Sync step
                        <input type="checkbox" id="ui-options-step">
                        <span class="checkmark"></span>
                    </label>
                    <label class="container">Sync batch index
                        <input type="checkbox" id="ui-options-bidx">
                        <span class="checkmark"></span>
                    </label>
                    <label class="container">Show axes
                        <input type="checkbox" id="ui-options-axes">
                        <span class="checkmark"></span>
                    </label>
                    <label class="container">Show ground
                        <input type="checkbox" id="ui-options-ground">
                        <span class="checkmark"></span>
                    </label>

                    <h3>Runs</h3>
                    <p id="logdir"></p>
                    <div id="run-selector"></div>

                    <h3>Tags</h3>
                    <div id="tag-selector"></div>
                </div>

                <div id="widget-view"> </div>
            </div>
            `;
    document.body.insertAdjacentHTML("beforeend", DASHBOARD_HTML);
    this.requestNewWindow();
  }

  /*  Request Open3D for a new window.
   *  @param {string} run Populate the window with data from this run. If
   *  undefined, the first run is chosen by default.
   */
  requestNewWindow = (run) => {
    // Ask Open3D for a new window
    window.console.info("Requesting window with size: (" + this.full_width +
      "," + this.full_height + ")");
    fetch(this.URL_ROUTE_PREFIX + "/new_window?width=" + this.full_width + "&height="
      + this.full_height, null)
      .then((response) => response.json())
      .then((response) => this.addConnection(response.window_id,
        response.logdir, run))
      .then(this.addAppEventListeners)
      .catch((err) => window.console.error("Error: /new_window failed:" + err));
  };

  /** Add App level event listeners.
  */
  addAppEventListeners = () => {
    window.addEventListener("beforeunload", this.closeWindow);
    // Listen for the user clicking on the main TB reload button
    let tbReloadButton =
      parent.document.querySelector(".reload-button");
    if (tbReloadButton != null) {
      tbReloadButton.addEventListener("click", this.reloadRunTags);
    }
    // App option handling
    document.getElementById("ui-options-view").addEventListener("change", (evt) => {
      if (evt.target.checked) {   // sync future viewpoint changes
        for (let [windowUId, webRtcClient] of this.webRtcClientList) {
          webRtcClient.sendJsonData = (jsonData) => {
            if (jsonData.class_name === "MouseEvent") {
              jsonData.class_name = "SyncMouseEvent";
              jsonData.window_uid_list = Array.from(
                this.webRtcClientList.keys());
            }
            webRtcClient.dataChannel.send(JSON.stringify(jsonData));
          }
        }
				this.messageId += 1;
				const syncViewMessage = {   // sync current viewpoint
					messageId: this.messageId,
					window_uid_list: Array.from(this.windowState.keys()),
					class_name: "tensorboard/sync_view",
				};
				window.console.info("Sending syncViewMessage:", syncViewMessage);
				this.webRtcClientList.values().next().value.sendJsonData(syncViewMessage);
      } else {
        for (let [windowUId, webRtcClient] of this.webRtcClientList) {
          webRtcClient.sendJsonData = WebRtcStreamer.prototype.sendJsonData;
        }
      }
    });

    document.getElementById("ui-options-step").addEventListener("change", (evt) => {
      if (evt.target.checked) {
        this.commonStep = this.windowState.values().next().value.step;
        for (let [windowUId, currentState] of this.windowState) {
          currentState.step = this.commonStep;
          this.requestGeometryUpdate(windowUId);
        }
      } else {
        this.commonStep = null;
      }
      window.console.debug("this.commonStep = ", this.commonStep);
    });

    document.getElementById("ui-options-bidx").addEventListener("change", (evt) => {
      if (evt.target.checked) {
        this.commonBatchIdx = this.windowState.values().next().value.batch_idx;
        for (let [windowUId, currentState] of this.windowState) {
          currentState.batch_idx = this.commonBatchIdx;
          this.requestGeometryUpdate(windowUId);
        }
      } else {
        this.commonBatchIdx = null;
      }
      window.console.debug("this.commonBatchIdx = ", this.commonBatchIdx);
    });

    document.getElementById("ui-options-axes").addEventListener("change", (evt) => {
      this.messageId += 1;
      const showHideAxesMessage = {
        messageId: this.messageId,
        window_uid_list: Array.from(this.windowState.keys()),
        class_name: "tensorboard/show_hide_axes",
        show: evt.target.checked
      };
      window.console.info("Sending showHideAxesMessage:", showHideAxesMessage);
      this.webRtcClientList.values().next().value.sendJsonData(showHideAxesMessage);
    });

    document.getElementById("ui-options-ground").addEventListener("change", (evt) => {
      this.messageId += 1;
      const showHideGroundMessage = {
        messageId: this.messageId,
        window_uid_list: Array.from(this.windowState.keys()),
        class_name: "tensorboard/show_hide_ground",
        show: evt.target.checked
      };
      window.console.info("Sending showHideGroundMessage:", showHideGroundMessage);
      this.webRtcClientList.values().next().value.sendJsonData(showHideGroundMessage);
    });
  };

  /**
   * Create a video element to display geometry and initiate WebRTC connection
   * with server. Attach listeners to process data channel messages.
   *
   * @param {string} windowUId  Window ID from the server, e.g.: "window_1"
   * @param {string} logdir TensorBoard log directory to be displayed to the
   *      user.
   * @param {string} run TB run to display here. Can be "undefined" in which
   *      case the server will assign a run.
   */
  addConnection = (windowUId, logdir, run) => {
    const videoId = "video_" + windowUId;
    let logdirElt = document.getElementById("logdir");
    logdirElt.innerText = logdir;

    // Add a video element to display WebRTC stream.
    const widgetTemplate = `
        <table class="webrtc" id="widget_${videoId}">
          <tr>
          <td><button id="zoom-${windowUId}" type="button">
            <svg viewBox="0 0 36 36" width="100%">
              <path d="m 10,16 2,0 0,-4 4,0 0,-2 L 10,10 l 0,6 0,0 z"></path>
              <path d="m 20,10 0,2 4,0 0,4 2,0 L 26,10 l -6,0 0,0 z"></path>
              <path d="m 24,24 -4,0 0,2 L 26,26 l 0,-6 -2,0 0,4 0,0 z"></path>
              <path d="M 12,20 10,20 10,26 l 6,0 0,-2 -4,0 0,-4 0,0 z"></path>
            </svg>
          </button></td>
          <td class="batchidx-step-selector">
            <div id="batch-idx-selector-div-${windowUId}"></div>
            <div id="step-selector-div-${windowUId}"></div>
          </td>
          <td><button id="settings-${windowUId}" type="button">
            <svg viewBox="0 0 24 24">
                <path d="M19.43 12.98c.04-.32.07-.64.07-.98s-.03-.66-.07-.98l2.11-1.65c.19-.15.24-.42.12-.64l-2-3.46c-.12-.22-.39-.3-.61-.22l-2.49 1c-.52-.4-1.08-.73-1.69-.98l-.38-2.65C14.46 2.18 14.25 2 14 2h-4c-.25 0-.46.18-.49.42l-.38 2.65c-.61.25-1.17.59-1.69.98l-2.49-1c-.23-.09-.49 0-.61.22l-2 3.46c-.13.22-.07.49.12.64l2.11 1.65c-.04.32-.07.65-.07.98s.03.66.07.98l-2.11 1.65c-.19.15-.24.42-.12.64l2 3.46c.12.22.39.3.61.22l2.49-1c.52.4 1.08.73 1.69.98l.38 2.65c.03.24.24.42.49.42h4c.25 0 .46-.18.49-.42l.38-2.65c.61-.25 1.17-.59 1.69-.98l2.49 1c.23.09.49 0 .61-.22l2-3.46c.12-.22.07-.49-.12-.64l-2.11-1.65zM12 15.5c-1.93 0-3.5-1.57-3.5-3.5s1.57-3.5 3.5-3.5 3.5 1.57 3.5 3.5-1.57 3.5-3.5 3.5z"></path>
            </svg>
          </button></td>
          </tr>
          <tr><td colspan=3><div id="loader_${videoId}"></div>
          <video id="${videoId}" muted="true" playsinline="true" width=${this.width} height=${this.height}>
            Your browser does not support HTML5 video.
          </video>
          </td></tr>
        </table>
        `;
    let widgetView = document.getElementById("widget-view");
    widgetView.insertAdjacentHTML("beforeend", widgetTemplate);

    let videoElt = document.getElementById(videoId);
    let client = new WebRtcStreamer(videoElt, this.URL_ROUTE_PREFIX, null, null);
    window.console.info("[addConnection] videoId: " + videoId);

    client.connect(windowUId, /*audio*/ null, this.webRtcOptions);
    this.webRtcClientList.set(windowUId, client);
    window.console.info("[addConnection] windowUId: " + windowUId);
    videoElt.addEventListener("LocalDataChannelOpen", (evt) => {
      evt.detail.channel.addEventListener("message",
        this.processDCMessage.bind(this, windowUId));
    });
    if (this.webRtcClientList.size === 1) {
      // Initial Run Tag reload only needed for first window
      videoElt.addEventListener("RemoteDataChannelOpen", this.reloadRunTags);
    } else { // Initialize state from first webrtcWindow
      // deep copy: All objects and sub-objects need to be copied with JS
      // spread syntax.
      let newWindowState = {...this.windowState.values().next().value};
      newWindowState.tags = [...newWindowState.tags];
      this.windowState.set(windowUId, newWindowState);
      if (run) {
        this.runWindow.set(run, windowUId);
        this.windowState.get(windowUId).run = run;
        window.console.debug("After addConnection: this.runWindow = ",
          this.runWindow, "this.windowState = ", this.windowState);
      }
      videoElt.addEventListener("RemoteDataChannelOpen",
        this.requestGeometryUpdate.bind(this, windowUId));
    }
    document.getElementById("zoom-"+ windowUId).addEventListener(
      "click", this.toggleZoom.bind(this, windowUId));
    document.getElementById("settings-"+ windowUId).addEventListener(
      "click", this.toggleSettings.bind(this, windowUId));
  };

  /* Callback to toggle WebRTC window size in the browser.
   */
	toggleZoom = (windowUId) => {
		let elem = document.getElementById("video_" + windowUId);
		if (elem.width <= this.width) {        // zoom
      elem.width = this.full_width;
      elem.height = this.full_height;
		} else {                                // original
      elem.width = this.width;
      elem.height = this.height;
		}
    window.console.debug(`New ${windowUId} size: (${elem.width}, ${elem.height})`);
	};

  /* Callback to toggle O3DVisualizer settings panel.
   */
	toggleSettings = (windowUId) => {
		this.messageId += 1;
		const toggleSettingsMessage = {
			messageId: this.messageId,
			window_uid: windowUId,
			class_name: "tensorboard/" + windowUId + "/toggle_settings",
		};
		window.console.info("Sending toggleSettingsMessage:", toggleSettingsMessage);
		this.webRtcClientList.get(windowUId).sendJsonData(toggleSettingsMessage);
	};


  /**
   * Create generic checkbox and radio button selectors used for Run and Tag
   * selectors respectively.
   * @param {string} name Selector name attribute.
   * @param {string} parentId Id of parent element.
   * @param {array} options Array of string options to create
   * @param {string} type "radio" (single selection) or "checkbox"
   * (multi-select).
   * @param {array} initialCheckedOptions Array of options to be shown
   * selected initially.
   */
  createSelector(name, parentId, options, type, initialCheckedOptions) {
    window.console.assert(type === "radio" || type === "checkbox",
      "type must be radio or checkbox");

    let parentElement = document.getElementById(parentId);
    parentElement.replaceChildren();  /* Remove existing children */
    initialCheckedOptions = new Set(initialCheckedOptions);
    for (const option of options) {
      let checked="";
      if(initialCheckedOptions.has(option)) {
        checked="checked";
      }
      const OPTION_TEMPLATE=`
            <label class="container">${option}
                <input type="${type}" id="${option}" name="${name}" ${checked}>
                <span class="checkmark"></span>
            </label>
            `;
      parentElement.insertAdjacentHTML("beforeend", OPTION_TEMPLATE);
      document.getElementById(option).addEventListener("change",
        this.onRunTagselect);
    }
  }

  /**
   * Create slider (range input element) for selecting a geometry in a batch,
   * or for selecting a step / iteration.
   * @param {int} windowUId Window ID associated with slider.
   * @param {string} name Selector name/id attribute.
   * @param {string} displayName Show this label in the UI.
   * @param {string} parentId Id of parent element.
   * @param {number} min Min value of range.
   * @param {number} max Max value of range.
   * @param {number} value Initial value of range.
   */
  createSlider(windowUId, name, displayName, parentId, min, max, value) {

    let parentElement = document.getElementById(parentId);
    parentElement.replaceChildren();  /* Remove existing children */
    const nameWid = name + "-" + windowUId;
    if (max > min) { // Don't create slider if no choice
      const sliderTemplate=`
            <form oninput="document.getElementById("${nameWid}_output").value
                = document.getElementById("${nameWid}").valueAsNumber;">
                <label> <span> ${displayName + ": [" + min.toString() + "-" +
                    max.toString() + "] "} </span>
                    <input type="range" id="${nameWid}" name="${nameWid}" min="${min}"
                        max="${max}" value="${value}">
                    </input>
                </label>
                <output for="${nameWid}" id="${nameWid + "_output"}"> ${value}
                </output>
            </form>
            `;
      parentElement.insertAdjacentHTML("beforeend", sliderTemplate);
      document.getElementById(nameWid).addEventListener("change",
        this.onStepBIdxSelect.bind(this, windowUId));
    }
  }

  /**
   * Event handler for browser tab / window close. Disconnect all server data
   * channel connections and close all server windows.
   */
  closeWindow = (evt) => {
    for (let [windowUId, webRtcClient] of this.webRtcClientList) {
      navigator.sendBeacon(this.URL_ROUTE_PREFIX + "/close_window?window_id=" +
        windowUId, null);
    }
    this.webRtcClientList.clear();
  };

  /**
   * Send a data channel message to the server to reload runs and tags from
   * the event file. Automatically run on loading.
   */
  reloadRunTags = () => {
    if (this.webRtcClientList.size > 0) {
      let client = this.webRtcClientList.values().next().value;
      // Any _open_ window_id may be used.
      const windowUId = this.webRtcClientList.keys().next().value;
      this.messageId += 1;
      const getRunTagsMessage = {
        messageId: this.messageId,
        window_id: windowUId,
        class_name: "tensorboard/" + windowUId + "/get_run_tags"
      };
      window.console.info("Sending getRunTagsMessage: ", getRunTagsMessage);
      client.dataChannel.send(JSON.stringify(getRunTagsMessage));
    } else {
      window.console.warn("No webRtcStreamer object initialized!");
    }
  };

/**
 * Send a data channel message to the server to request an update to the
 * geometry display.
 */
requestGeometryUpdate = (windowUId) => {
  this.messageId += 1;
  const updateGeometryMessage = {
    messageId: this.messageId,
    window_uid: windowUId,
    class_name: "tensorboard/" + windowUId + "/update_geometry",
    run: this.windowState.get(windowUId).run,
    tags: Array.from(this.selectedTags),
    batch_idx: this.commonBatchIdx || this.windowState.get(windowUId).batch_idx,
    step: this.commonStep || this.windowState.get(windowUId).step
  };
  window.console.info("Sending updateGeometryMessage:", updateGeometryMessage);
  this.webRtcClientList.get(windowUId).sendJsonData(updateGeometryMessage);
  // add busy indicator
  document.getElementById("loader_video_" + windowUId).classList.add("loader");
};

/**
 * Event handler for Run and Tags selector update. Triggers a geometry
 * update message.
 */
onRunTagselect = (evt) => {
  if (evt.target.name === "run-selector-checkboxes") {
    if (this.runWindow.has(evt.target.id)) {
      const windowUId = this.runWindow.get(evt.target.id);
      let windowWidget = document.getElementById("widget_video_" + windowUId);
      if (evt.target.checked) { // display window
        windowWidget.style.display = "flex";
        window.console.info("Showing window " + windowUId + " with run " + evt.target.id);
      } else {    // hide window
        windowWidget.style.display = "none";
        window.console.info("Hiding window " + windowUId + " with run " + evt.target.id);
      }
    } else {    // create new window
      this.requestNewWindow(evt.target.id);
    }
  } else if (evt.target.name ===   "tag-selector-checkboxes") {
    if (evt.target.checked) {
      this.selectedTags.add(evt.target.id);
    } else {
      this.selectedTags.delete(evt.target.id);
    }
    for (const windowUId of this.windowState.keys()) {
      this.requestGeometryUpdate(windowUId);
    }
  }
};

/**
 * Event handler for Step and Batch Index selector update. Triggers a
 * geometry update message.
 */
onStepBIdxSelect = (windowUId, evt) => {
  window.console.debug("[onStepBIdxSelect] this.windowState: ",
    this.windowState, "this.commonStep", this.commonStep,
    "this.commonBatchIdx", this.commonBatchIdx);
  if (evt.target.name.startsWith("batch-idx-selector")) {
    if (this.commonBatchIdx != null) {
      this.commonBatchIdx = evt.target.value;
      for (const windowUId of this.windowState.keys()) {
        this.requestGeometryUpdate(windowUId);
      }
    } else {
      this.windowState.get(windowUId).batch_idx = evt.target.value;
      this.requestGeometryUpdate(windowUId);
    }
  } else if (evt.target.name.startsWith("step-selector")) {
    if (this.commonStep != null) {
      this.commonStep = evt.target.value;
      for (const windowUId of this.windowState.keys()) {
        this.requestGeometryUpdate(windowUId);
      }
    } else {
      this.windowState.get(windowUId).step = evt.target.value;
      this.requestGeometryUpdate(windowUId);
    }
  }
};


/**
 * Data channel message handler. Updates UI controls based on server state.
 */
processDCMessage = (windowUId, evt) => {
  let message = null;
  try {
    message = JSON.parse(evt.data);
  } catch (err) {
    if (err.name === "SyntaxError") {
      if (evt.data.endsWith("DataChannel open")) {
        return;
      }
      if (evt.data.startsWith("[Open3D WARNING]")) {
        window.console.warn(evt.data);
        return;
      }
    }
  }
  if (message.class_name.endsWith("get_run_tags")) {
    const runToTags = message.run_to_tags;
    this.createSelector("run-selector-checkboxes", "run-selector",
      Object.getOwnPropertyNames(runToTags), "checkbox",
      [message.current.run]);
    this.selectedTags = new Set(message.current.tags);
    let allTags = new Set();
    for (const run in runToTags) {
      for (const tag of runToTags[run]) {
        allTags.add(tag);
      }
    }
    this.createSelector("tag-selector-checkboxes", "tag-selector",
      allTags, "checkbox", this.selectedTags);

    if (this.windowState.size === 0) { // First load
        this.runWindow.set(message.current.run, windowUId);
        this.windowState.set(windowUId, message.current);
        window.console.debug("[After get_run_tags] this.runWindow: ",
          this.runWindow, "this.windowState:", this.windowState);
    }
    for (const [windowUId, state] of this.windowState) {
      if (runToTags.hasOwnProperty(state.run)) { // update window
        this.requestGeometryUpdate(windowUId);
      } else {   // stale run: close window
        fetch(this.URL_ROUTE_PREFIX +  "/close_window?window_id=" + windowUId, null)
          .then((response) => response.json())
          .then((response) => window.console.log(response))
          .then(this.runWindow.delete(state.run))
          .then(this.windowState.delete(windowUId))
          .then(this.webRtcClientList.delete(windowUId))
          .then(document.getElementById("widget_video_" + windowUId).remove())
          .catch((err) => window.console.error("Error closing widget:", err));
      }
    }
  } else if (message.class_name.endsWith("update_geometry")) {
    // Sync state with server
    window.console.assert(message.window_uid === windowUId,
      `windowUId mismatch: received ${message.window_uid} !== ${windowUId}`);
    this.windowState.set(windowUId, message.current);
    window.console.debug("[After update_geometry] this.runWindow: ",
      this.runWindow, "this.windowState:", this.windowState);
    this.createSlider(windowUId, "batch-idx-selector", "Batch index",
      "batch-idx-selector-div-" + windowUId, 0,
      message.current.batch_size - 1, message.current.batch_idx);
    this.createSlider(windowUId, "step-selector", "Step",
      "step-selector-div-" + windowUId, message.current.step_limits[0],
      message.current.step_limits[1], message.current.step);
    // Init with miliseconds
    const wallTime = new Date(message.current.wall_time * 1000);
    document.getElementById("video_" + windowUId).title = message.current.run
      + " at " + wallTime.toLocaleString();
    // remove busy indicator
    document.getElementById("loader_video_" + windowUId).classList.remove("loader");
  }
};

}

/**
 * The main entry point of any TensorBoard iframe plugin.
 */
export async function render() {
  const o3dclient = new TensorboardOpen3DPluginClient();
}
