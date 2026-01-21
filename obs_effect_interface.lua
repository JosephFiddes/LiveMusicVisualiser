obs = obslua

local Effect_Interface = {}
Effect_Interface.__index = Effect_Interface

local function bind(obj, fn)
    return function(...)
        return fn(obj, ...)
    end
end

function Effect_Interface.new(id, name, flags, create, destroy)
    local self = setmetatable({}, Effect_Interface)

    self.source_info = {}
    self.source_info.id = id
    self.source_info.type = obs.OBS_SOURCE_TYPE_FILTER
    self.source_info.output_flags = flags
    self.source_info.get_name = function() return name end

    self.implementation = {}
    self.implementation.create = create
    self.implementation.destroy = destroy

    self.source_info.create = bind(self, Effect_Interface._create_impl)
    self.source_info.destroy = bind(self, Effect_Interface._destroy_impl)
    self.source_info.get_defaults = bind(self, Effect_Interface._get_defaults_impl)
    self.source_info.get_properties = bind(self, Effect_Interface._get_properties_impl)
    self.source_info.update = bind(self, Effect_Interface._update_impl)
    self.source_info.video_render = bind(self, Effect_Interface._video_render_impl)

    self.params = {}

    return self
end

function Effect_Interface:add_parameter(name, disp_name, 
    on_create, get_default, get_properties, on_update, on_video_render)
    local param = {
        name = name,
        disp_name = disp_name,
        on_create = on_create,
        get_default = get_default,
        get_properties = get_properties,
        on_update = on_update,
        on_video_render = on_video_render
    }

    table.insert(self.params, param)
end

function Effect_Interface._create_impl(self, settings, source)
    data = self.implementation.create(settings, source)

    for _, param in ipairs(self.params) do
        param.on_create(param.name, param.disp_name, data)
    end

    self.source_info.update(data, settings)

    return data
end

function Effect_Interface._destroy_impl(self, data)
    self.implementation.destroy(data)
end

function Effect_Interface._get_defaults_impl(self, settings)
    for _, param in ipairs(self.params) do
        param.get_default(param.name, param.disp_name, settings)
    end
end

function Effect_Interface._get_properties_impl(self, data)
    local props = obs.obs_properties_create()

    for _, param in ipairs(self.params) do
        param.get_properties(param.name, param.disp_name, props, data)
    end

    return props
end

function Effect_Interface._update_impl(self, data, settings)
    for _, param in ipairs(self.params) do
        param.on_update(param.name, param.disp_name, data, settings)
    end
    data.settings = settings
end

function Effect_Interface._video_render_impl(self, data)
    
	local parent = obs.obs_filter_get_parent(data.source)
	data.width = obs.obs_source_get_base_width(parent)
	data.height = obs.obs_source_get_base_height(parent)

	obs.obs_source_process_filter_begin(data.source, obs.GS_RGBA, obs.OBS_NO_DIRECT_RENDERING)
    
    for _, param in ipairs(self.params) do
        param.on_video_render(param.name, param.disp_name, data)
    end
    
	obs.obs_source_process_filter_end(data.source, data.effect, data.width, data.height)
end

function Effect_Interface:register_source()
    obs.obs_register_source(self.source_info)
end

return Effect_Interface