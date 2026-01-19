obs = obslua

Effect_Interface = {
    source_info = {},
    implementation = {},
    properties = {}
}

function Effect_Interface:new(id, name, flags, create, destroy)
    self.source_info = {}
    self.source_info.id = id
    self.source_info.type = obs.OBS_SOURCE_TYPE_FILTER
    self.source_info.output_flags = flags
    self.source_info.get_name = function() return name end

    self.implementation.create = create
    self.implementation.destroy = destroy

    self.source_info.create = function(settings, source) self.__create(setting, source); end
    self.source_info.destroy = function(data) self.__destroy(data); end
    self.source_info.update = function(data, settings), self.__update(data, settings); end
    self.source_info.video_render = function(data), self.__video_render(data); end


end

function Effect_Interface:__create(settings, source)

    data = self.implementation.create(settings, source)

    data.params = -- some function to get all the params (iterates through a bunch of callbacks)

    self.source_info.update(data, settings)
end

function Effect_Interface:__destroy(data)
    self.implementation.destroy(data)
end

function Effect_Interface:__update(data, settings)
    data.thingo -- iterate through, getting data

    data.settings = settings
end

function Effect_Interface:__video_render(data)
    
	local parent = obs.obs_filter_get_parent(data.source)
	data.width = obs.obs_source_get_base_width(parent)
	data.height = obs.obs_source_get_base_height(parent)

	obs.obs_source_process_filter_begin(data.source, obs.GS_RGBA, obs.OBS_NO_DIRECT_RENDERING)

    -- do some iterative stuff here
    
	obs.obs_source_process_filter_end(data.source, data.effect, data.width, data.height)
end