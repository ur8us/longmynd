'use strict';

const ws_url = "ws://" + window.location.hostname + ":" + window.location.port + "/";
const DEFAULT_LO_FREQUENCY_KHZ = 9360000;
const QO100_BEACON_FREQUENCY_KHZ = 10491500;
const QO100_BEACON_SYMBOLRATE_KS = 1500;

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
let config_initialized = false;

function load_settings()
{
  if(typeof(Storage) === "undefined")
  {
    return;
  }

  const storage_lo_frequency = localStorage.getItem("longmynd-lo-frequency");
  if(storage_lo_frequency != null)
  {
    try
    {
      const stored_lo_frequency = JSON.parse(storage_lo_frequency);
      if(!isNaN(stored_lo_frequency))
      {
        lo_frequency = stored_lo_frequency;
      }
    }
    catch(e)
    {
      console.log("Error parsing storage_lo_frequency!", e);
    }
  }
}

function save_settings()
{
  if(typeof(Storage) !== "undefined")
  {
    localStorage.setItem("longmynd-lo-frequency", JSON.stringify(lo_frequency));
  }
}

$(document).ready(function()
{
  load_settings();
  $("#input-frequency-lo").val(lo_frequency);

  /* Set up configure */
  $("#submit-freq-sr").click(function(e)
  {
    e.preventDefault();

    let input_frequency_value = parseInt($("#input-frequency").val());

    if(isNaN(input_frequency_value))
    {
      input_frequency_value = parseInt($("#input-qo100frequency").val()) - lo_frequency;
    }
    let input_symbolrate_value = parseInt($("#input-symbolrate").val());

    if(input_frequency_value != 0 && input_symbolrate_value != 0)
    {
      ws_control.sendMessage("C"+input_frequency_value+","+input_symbolrate_value);
    }
  });
  $("#beacon-freq-sr").click(function(e)
  {
    e.preventDefault();
    $("#input-qo100frequency").val(QO100_BEACON_FREQUENCY_KHZ);
    $("#input-frequency").val("");
    $("#input-symbolrate").val(QO100_BEACON_SYMBOLRATE_KS);
  });

  $("#input-frequency-lo").keyup(function()
  {
    const input_lo_frequency = parseInt($("#input-frequency-lo").val(), 10);

    if(!isNaN(input_lo_frequency))
    {
      $("#input-frequency-lo").removeClass("is-invalid");
      lo_frequency = input_lo_frequency;
      save_settings();
    }
    else
    {
      $("#input-frequency-lo").addClass("is-invalid");
    }
  });
  /*
  {"type":"status","timestamp":1571256202.388,"packet":{"rx":{"demod_state":4,"frequency":742530,"symbolrate":1998138,
  "vber":0,"ber":1250,"mer":80,"modcod":6,"short_frame":false,"pilot_symbols":true,
  "constellation":[[221,227],[19,213],[35,44],[203,213],[51,62],[77,221],[229,219],[234,35],[199,57],[31,230],[216,210],[228,38],[24,221],[247,31],[230,207],[237,203]]},
  "ts":{"service_name":"A71A","service_provider_name":"QARS","null_ratio":0,"PIDs":[[257,27],[258,3]]}}}
*/
  /* Render to fields */
  function render_status(data_json)
  {
    let status_obj;
    let status_packet;
    try {
      status_obj = JSON.parse(data_json);
      if(status_obj != null)
      {
        //console.log(status_obj);
        rx_status = status_obj.packet.rx;

        if(!config_initialized)
        {
          if($("#input-frequency").val() === "")
          {
            $("#input-frequency").val(rx_status.frequency_requested);
          }
          if($("#input-symbolrate").val() === "")
          {
            $("#input-symbolrate").val(rx_status.symbolrate_requested);
          }
          config_initialized = true;
        }

        let rflevel_dbm = rflevel_lookupfn(rx_status.agc1, rx_status.agc2);
        $("#valuedisplay-rflevel").text(rflevel_dbm+"dBm");
        $("#progressbar-rflevel").css('width', ((rflevel_dbm+40)*(100.0/35)+'%')).attr('aria-valuenow', rflevel_dbm);

        $("#badge-state").text(demod_state_lookup[rx_status.demod_state]);
        $("#span-status-frequency").text(rx_status.frequency+"KHz");
        $("#span-status-symbolrate").text(rx_status.symbolrate+"KS");
        if(rx_status.demod_state == 3) // DVB-S
        {
          $("#span-status-modcod").text(modcod_lookup_dvbs[rx_status.modcod]);
        }
        else if(rx_status.demod_state == 4) // DVB-S2
        {
          $("#span-status-modcod").text(modcod_lookup_dvbs2[rx_status.modcod]);
        }
        else
        {
          $("#span-status-modcod").text("");
        }
        $("#progressbar-mer").css('width', (rx_status.mer/3.1)+'%').attr('aria-valuenow', rx_status.mer).text(rx_status.mer/10.0+"dB");

        $("#progressbar-vber").css('width', (rx_status.vber)+'%').attr('aria-valuenow', rx_status.vber).text(rx_status.vber/10.0+"%");

        $("#progressbar-ber").css('width', (rx_status.ber)+'%').attr('aria-valuenow', rx_status.ber).text(rx_status.ber/10.0+"%");

        constellation_draw(rx_status.constellation);

        ts_status = status_obj.packet.ts;

        $("#progressbar-ts-null").css('width', (ts_status.null_ratio)+'%').attr('aria-valuenow', ts_status.null_ratio).text(ts_status.null_ratio+"%");

        $("#span-status-name").text(ts_status.service_name);
        $("#span-status-provider").text(ts_status.service_provider_name);

        let ulTsPids = $('<ul />');
        for (pid in ts_status.PIDs) {
          $('<li />')
            .text(ts_status.PIDs[pid][0]+": "+mpeg_type_lookup[ts_status.PIDs[pid][1]])
            .appendTo(ulTsPids);
        }
        $("#div-ts-pids").empty();
        $("#div-ts-pids").append(ulTsPids);

      }
    }
    catch(e)
    {
      console.log("Error parsing message!",e);
    }
  }


  /* Set up listener for websocket */
  render_timer = setInterval(function()
  {
    if(!render_busy)
    {
      render_busy = true;
      if(ws_monitor_buffer.length > 0)
      {
        /* Pull newest data off the buffer and render it */
        let data_frame = ws_monitor_buffer.pop();

        render_status(data_frame);

        ws_monitor_buffer.length = 0;
      }
      render_busy = false;
    }
    else
    {
      console.log("Slow render blocking next frame, configured interval is ", render_interval);
    }
  }, render_interval);
});
