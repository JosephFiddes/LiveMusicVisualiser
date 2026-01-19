--[[
Should create function definitions for:
    script_description()
    source_info.create = function(settings, source) end
    source_info.destroy = function(data) end
    source_info.get_defaults = function(settings) end
    source_info.get_properties = function(data) end
    source_info.update = function(data, settings) end
    source_info.video_render = function(data) end
    script_load()

A few helpful functions have also been included.
]]

obs = obslua

-- Overwrite with description of the effect:
-- return a html formatted description of the effect
function script_description()
	return [[<center><h2>Example Description</h2></center>
		<p>This Lua script has some sort of effect on a <it>source</it>. 
        The details are described here.</p>]]
end

-- GLOBALS
source_info = {}
source_info.id = "filter-example"
source_info.type = obs.OBS_SOURCE_TYPE_FILTER
source_info.output_flags = obs.OBS_SOURCE_VIDEO
source_info.get_name = function() return "Example filter" end

-- Creates the implementation data for the source
-- If implements HLSL code, see details below
source_info.create = function(settings, source)
	-- Initializes the custom data table
	local data = {}
	data.source = source
	data.width = 1
	data.height = 1

	-- Compiles the effect
	obs.obs_enter_graphics()
	local effect_file_path = script_path() .. 'example.hlsl'
	data.effect = obs.gs_effect_create_from_file(effect_file_path, nil)
	obs.obs_leave_graphics()

	-- Calls the destroy function if the effect was not compiled properly
	if data.effect == nil then
		obs.blog(obs.LOG_ERROR, "Effect compilation failed for " .. effect_file_path)
		source_info.destroy(data)
		return nil
	end

	-- Retrieve the shader uniform variables
	data.params = {}
	data.params.example = obs.gs_effect_get_param_by_name(data.effect, "example")

	-- Calls update to initialize the rest of the properties-managed settings
	source_info.update(data, settings)

	return data
end

-- Destroys and release resources linked to the custom data
source_info.destroy = function(data)
	if data.effect == nil then return end

	obs.obs_enter_graphics()
	obs.gs_effect_destroy(data.effect)
	data.effect = nil
	obs.obs_leave_graphics()
end


-- Sets the default settings for this source
source_info.get_defaults = function(settings)
  obs.obs_data_set_default_double(settings, "example", 1.0)
  -- obs.obs_data_set_default_int(settings, "example", 1)
  -- obs.obs_data_set_default_string(settings, "example", "hello world")
end

-- Gets the property information of this source
source_info.get_properties = function(data)
  local props = obs.obs_properties_create()
  obs.obs_properties_add_float_slider(props, "example", "Example slider", 1.0, 2.2, 0.2)
  -- obs.obs_properties_add_int_slider(props, "example", "Example slider", 2, 10, 1)

  obs.obs_properties_add_path(props, "example_image_path", "Example image path", obs.OBS_PATH_FILE,
                               "Example (*.png *.bmp *.jpg *.gif)", nil)
  
  obs.obs_properties_add_button(props, "example_button", "Example button", function()
     do_something(); return true; end)
  return props
end

-- Updates the internal data for this source upon settings change
-- Called when a setting has been changed. (NOT PER FRAME)
source_info.update = function(data, settings)
  data.example = obs.obs_data_get_double(settings, "example")
  -- data.example = obs.obs_data_get_int(settings, "example")

  -- Keeps a reference on the settings
  data.settings = settings

  -- Loads a texture only if changed (otherwise keeps previous texture).
  local example_image_path = obs.obs_data_get_string(settings, "example_image_path")
  if data.loaded_example_image_path ~= example_image_path then
    data.example_image = load_texture(example_image_path, data.example_image)
    data.loaded_example_image_path = example_image_path
  end
end

-- Called when rendering the source with the graphics subsystem
source_info.video_render = function(data)
	local parent = obs.obs_filter_get_parent(data.source)
	data.width = obs.obs_source_get_base_width(parent)
	data.height = obs.obs_source_get_base_height(parent)

	obs.obs_source_process_filter_begin(data.source, obs.GS_RGBA, obs.OBS_NO_DIRECT_RENDERING)

	-- Effect parameters initialization goes here
	obs.gs_effect_set_int(data.params.example, data.example)
	-- obs.gs_effect_set_float(data.params.example, data.example)
    
	-- Pattern texture
	set_texture_effect_parameters(data.pattern, data.params.pattern_texture, data.params.pattern_size)
	obs.gs_effect_set_float(data.params.pattern_gamma, data.pattern_gamma)

	obs.obs_source_process_filter_end(data.source, data.effect, data.width, data.height)

end

function script_load(settings)
	obs.obs_register_source(source_info)
end

-- USEFUL HELPER FUNCTIONS (USE UNCHANGED)

-- For HLSL code that seeks an image
function set_texture_effect_parameters(image, param_texture, param_size)
	local size = obs.vec2()

	if image then
		obs.gs_effect_set_texture(param_texture, image.texture)
		obs.vec2_set(size, image.cx, image.cy)
	else
		obs.vec2_set(size, -1, -1)
	end

	obs.gs_effect_set_vec2(param_size, size)
end

-- Returns new texture and free current texture if loaded
function load_texture(path, current_texture)

  obs.obs_enter_graphics()

  -- Free any existing image
  if current_texture then
    obs.gs_image_file_free(current_texture)
  end

  -- Loads and inits image for texture
  local new_texture = nil
  if string.len(path) > 0 then
    new_texture = obs.gs_image_file()
    obs.gs_image_file_init(new_texture, path)
    if new_texture.loaded then
      obs.gs_image_file_init_texture(new_texture)
    else
      obs.blog(obs.LOG_ERROR, "Cannot load image " .. path)
      obs.gs_image_file_free(current_texture)
      new_texture = nil
    end
  end

  obs.obs_leave_graphics()
  return new_texture
end


-- Returns the width of the source
source_info.get_width = function(data)
	return data.width
end

-- Returns the height of the source
source_info.get_height = function(data)
	return data.height
end