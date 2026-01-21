obs = obslua

local Effect_Interface = require("obs_effect_interface")

function script_description()
	return [[<center><h2>Sound Visualiser</h2></center>
		<p>Visualise sound.</p>]]
end

function script_load(settings)
    local sound_visualiser_interface = Effect_Interface.new("sound-vis", "Sound Visualiser", 
        obs.OBS_SOURCE_VIDEO, 
		-- create()
		function(settings, source) 
			-- Initializes the custom data table
			local data = {}
			data.source = source
			data.width = 1
			data.height = 1
			
			data.params = {}

			return data
		end, 
		-- destroy()
		function(data) 
			if data.effect == nil then return end

			obs.obs_enter_graphics()
			obs.gs_effect_destroy(data.effect)
			data.effect = nil
			obs.obs_leave_graphics()
		end)

	sound_visualiser_interface:add_parameter("gamma", "Gamma encoding exponent", 
		-- on_create() (called in source_info.create)
		function(name, disp_name, data) 
			-- data.params.gamma = obs.gs_effect_get_param_by_name(data.effect, name)
			data.params.gamma = "it's gammaing time"
		end,
		-- get_default() (called in source_info.get_defaults)
		function(name, disp_name, settings) 
  			obs.obs_data_set_default_double(settings, name, 1.0)
		end,
		-- get_properties() (called in source_info.get_properties)
		function(name, disp_name, props, data) 
  			obs.obs_properties_add_float_slider(props, name, disp_name, 1.0, 2.2, 0.2)
		end,
		-- on_update() (called in source_info.update)
		function(name, disp_name, data, settings) 
			data.gamma = obs.obs_data_get_double(settings, name)
		end,
		-- on_video_render() (called in source_info.video_render)
		function(name, disp_name, data) 
			-- obs.gs_effect_set_float(data.params.gamma, data.gamma)
		end)

	sound_visualiser_interface:register_source()
end
