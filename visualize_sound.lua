function script_description()
	return [[<center><h2>Sound Visualiser</h2></center>
		<p>Visualise sound.</p>]]
end

function script_load(settings)
    sound_visualiser_interface = Effect_Interface:new("sound-vis", "Sound Visualiser", 
        obs.OBS_SOURCE_VIDEO, function() return 0 end, function() return 0 end)

    

	sound_visualiser_interface.register_source()
end
