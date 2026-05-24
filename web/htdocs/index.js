"use strict";

const ws_url =
  "ws://" + window.location.hostname + ":" + window.location.port + "/";
const DEFAULT_LO_FREQUENCY_KHZ = 9360000;
const QO100_BEACON_FREQUENCY_KHZ = 10491500;
const QO100_BEACON_SYMBOLRATE_KS = 1500;
const LOW_SR_MODE_AUTO = 0;
const LOW_SR_MODE_ON = 1;
const LOW_SR_MODE_OFF = 2;

let ws_monitor_buffer = [];
let ws_control_buffer = [];

let ws_monitor = new strWebsocket(ws_url, "monitor", ws_monitor_buffer);
let ws_control = new strWebsocket(ws_url, "control", ws_control_buffer);

let render_timer = null;
let render_busy = false;
let render_interval = 100;

let rx_status = null;
let ts_status = null;
let lo_frequency = DEFAULT_LO_FREQUENCY_KHZ;
let low_sr_mode = LOW_SR_MODE_AUTO;
let config_initialized = false;

function set_low_sr_buttons(mode, active) {
  const buttons = {
    [LOW_SR_MODE_AUTO]: $("#button-low-sr-auto"),
    [LOW_SR_MODE_ON]: $("#button-low-sr-on"),
    [LOW_SR_MODE_OFF]: $("#button-low-sr-off"),
  };

  for (const key in buttons) {
    buttons[key].removeClass("active");
    buttons[key].find("input").prop("checked", false);
  }

  if (buttons[mode] !== undefined) {
    buttons[mode].addClass("active");
    buttons[mode].find("input").prop("checked", true);
  }

  $("#span-low-sr-active").text(active ? "on" : "off");
}

function send_low_sr_mode(mode) {
  low_sr_mode = mode;
  set_low_sr_buttons(mode, mode !== LOW_SR_MODE_OFF);
  ws_control.sendMessage("L" + mode);
}

function load_settings() {
  if (typeof Storage === "undefined") {
    return;
  }

  const storage_lo_frequency = localStorage.getItem("longmynd-lo-frequency");
  if (storage_lo_frequency != null) {
    try {
      const stored_lo_frequency = JSON.parse(storage_lo_frequency);
      if (!isNaN(stored_lo_frequency)) {
        lo_frequency = stored_lo_frequency;
      }
    } catch (e) {
      console.log("Error parsing storage_lo_frequency!", e);
    }
  }
}

function save_settings() {
  if (typeof Storage !== "undefined") {
    localStorage.setItem("longmynd-lo-frequency", JSON.stringify(lo_frequency));
  }
}

$(document).ready(function () {
  load_settings();
  $("#input-frequency-lo").val(lo_frequency);

  /* Set up configure */
  $("#submit-freq-sr").click(function (e) {
    e.preventDefault();

    let input_frequency_value = parseInt($("#input-frequency").val());

    if (isNaN(input_frequency_value)) {
      input_frequency_value =
        parseInt($("#input-qo100frequency").val()) - lo_frequency;
    }
    let input_symbolrate_value = parseInt($("#input-symbolrate").val());

    if (input_frequency_value != 0 && input_symbolrate_value != 0) {
      ws_control.sendMessage(
        "C" + input_frequency_value + "," + input_symbolrate_value,
      );
    }
  });
  $("#beacon-freq-sr").click(function (e) {
    e.preventDefault();
    $("#input-qo100frequency").val(QO100_BEACON_FREQUENCY_KHZ);
    $("#input-frequency").val("");
    $("#input-symbolrate").val(QO100_BEACON_SYMBOLRATE_KS);
  });

  $("#button-low-sr-auto").click(function () {
    send_low_sr_mode(LOW_SR_MODE_AUTO);
  });
  $("#button-low-sr-on").click(function () {
    send_low_sr_mode(LOW_SR_MODE_ON);
  });
  $("#button-low-sr-off").click(function () {
    send_low_sr_mode(LOW_SR_MODE_OFF);
  });

  $("#input-frequency-lo").keyup(function () {
    const input_lo_frequency = parseInt($("#input-frequency-lo").val(), 10);

    if (!isNaN(input_lo_frequency)) {
      $("#input-frequency-lo").removeClass("is-invalid");
      lo_frequency = input_lo_frequency;
      save_settings();
    } else {
      $("#input-frequency-lo").addClass("is-invalid");
    }
  });

  /* Tuner Gain Slider */
  $("#input-gain").on("input", function () {
    let gain = parseInt($(this).val());
    if (gain > 0) {
      $("#gain-value").text(gain + "/15");
      ws_control.sendMessage("G" + gain);
    } else {
      $("#gain-value").text("Auto");
      ws_control.sendMessage("G0");
    }
  });
  /*
  {"type":"status","timestamp":1571256202.388,"packet":{"rx":{"demod_state":4,"frequency":742530,"symbolrate":1998138,
  "vber":0,"ber":1250,"mer":80,"modcod":6,"short_frame":false,"pilot_symbols":true,
  "constellation":[[221,227],[19,213],[35,44],[203,213],[51,62],[77,221],[229,219],[234,35],[199,57],[31,230],[216,210],[228,38],[24,221],[247,31],[230,207],[237,203]]},
  "ts":{"service_name":"A71A","service_provider_name":"QARS","null_ratio":0,"PIDs":[[257,27],[258,3]]}}}
*/
  /* Render to fields */
  function render_status(data_json) {
    let status_obj;
    let status_packet;
    try {
      status_obj = JSON.parse(data_json);
      if (status_obj != null) {
        //console.log(status_obj);
        rx_status = status_obj.packet.rx;

        if (!config_initialized) {
          if ($("#input-frequency").val() === "") {
            $("#input-frequency").val(rx_status.frequency_requested);
          }
          if ($("#input-symbolrate").val() === "") {
            $("#input-symbolrate").val(rx_status.symbolrate_requested);
          }
          config_initialized = true;
        }

        if (rx_status.low_sr_mode !== undefined) {
          low_sr_mode = Math.round(rx_status.low_sr_mode);
          set_low_sr_buttons(low_sr_mode, rx_status.low_sr_active === true);
        }

        let rflevel_dbm = rflevel_lookupfn(rx_status.agc1, rx_status.agc2);
        $("#valuedisplay-rflevel").text(rflevel_dbm + "dBm");
        $("#progressbar-rflevel")
          .css("width", (rflevel_dbm + 40) * (100.0 / 35) + "%")
          .attr("aria-valuenow", rflevel_dbm);

        $("#badge-state").text(demod_state_lookup[rx_status.demod_state]);
        $("#span-status-frequency").text(rx_status.frequency + "KHz");

        if (rx_status.tuner_gain !== undefined) {
          let gain = rx_status.tuner_gain;
          $("#input-gain").val(gain);
          if (gain > 0) {
            $("#gain-value").text(gain + "/15");
          } else {
            $("#gain-value").text("Auto");
          }
        }
        $("#span-status-symbolrate").text(rx_status.symbolrate / 1000.0 + "KS");
        if (rx_status.demod_state == 3) // DVB-S
        {
          $("#span-status-modcod").text(modcod_lookup_dvbs[rx_status.modcod]);
        } else if (rx_status.demod_state == 4) // DVB-S2
        {
          $("#span-status-modcod").text(modcod_lookup_dvbs2[rx_status.modcod]);
        } else {
          $("#span-status-modcod").text("");
        }
        $("#progressbar-mer")
          .css("width", rx_status.mer / 3.1 + "%")
          .attr("aria-valuenow", rx_status.mer)
          .text(rx_status.mer / 10.0 + "dB");

        $("#progressbar-vber")
          .css("width", rx_status.vber + "%")
          .attr("aria-valuenow", rx_status.vber)
          .text(rx_status.vber / 10.0 + "%");

        $("#progressbar-ber")
          .css("width", rx_status.ber + "%")
          .attr("aria-valuenow", rx_status.ber)
          .text(rx_status.ber / 10.0 + "%");

        if (rx_status.constellation)
          constellation_draw(rx_status.constellation);

        if (status_obj.packet.ts) {
          ts_status = status_obj.packet.ts;

          let format_bitrate = function (bps) {
            if (bps >= 1000000) return (bps / 1000000.0).toFixed(2) + " Mbps";
            if (bps >= 1000) return (bps / 1000.0).toFixed(1) + " Kbps";
            return bps + " bps";
          };

          $("#span-status-bitrate-total").text(
            format_bitrate(ts_status.bitrate_total),
          );
          $("#span-status-bitrate-useful").text(
            format_bitrate(ts_status.bitrate_useful),
          );

          $("#progressbar-density")
            .css("width", 100.0 - ts_status.null_ratio + "%")
            .attr("aria-valuenow", 100.0 - ts_status.null_ratio)
            .text((100.0 - ts_status.null_ratio).toFixed(1) + "%");

          $("#span-status-name").text(ts_status.service_name);
          $("#span-status-provider").text(ts_status.service_provider_name);

          try {
            console.log("mpeg_type_lookup:", mpeg_type_lookup);
            $("#div-ts-pids").empty();
            if (ts_status.PIDs) {
              for (let pid in ts_status.PIDs) {
                let pid_num = ts_status.PIDs[pid][0];
                let type_label = "";
                if (pid_num == -1) {
                  type_label = "Overhead";
                } else {
                  type_label = mpeg_type_lookup[ts_status.PIDs[pid][1]];
                  if (type_label === undefined) type_label = "Unknown";
                }

                let occupancy = ts_status.PIDs[pid][2]
                  ? (ts_status.PIDs[pid][2] / 10.0).toFixed(1) + "%"
                  : "0%";

                let text =
                  pid_num == -1
                    ? occupancy + ": Overhead"
                    : pid_num + " (" + occupancy + "): " + type_label;

                $("<div />")
                  .css("margin-left", "10px")
                  .text(text)
                  .appendTo($("#div-ts-pids"));
              }
            }
          } catch (e) {
            console.log("Error rendering PIDs", e);
          }
        }
      }
    } catch (e) {
      console.log("Error parsing message!", e);
    }
  }

  /* Set up listener for websocket */
  render_timer = setInterval(function () {
    if (!render_busy) {
      render_busy = true;
      if (ws_monitor_buffer.length > 0) {
        /* Pull newest data off the buffer and render it */
        let data_frame = ws_monitor_buffer.pop();

        render_status(data_frame);

        ws_monitor_buffer.length = 0;
      }
      render_busy = false;
    } else {
      console.log(
        "Slow render blocking next frame, configured interval is ",
        render_interval,
      );
    }
  }, render_interval);
});
