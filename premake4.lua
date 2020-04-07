solution "Raygecaster"
	configurations {"Debug", "Release"}
	location "build"
	project "Raygecaster"
		language "C"
		kind "ConsoleApp"
		files {"src/**.h", "src/**.c"}
		--Target specific rules
		configuration "codelite"
			links {"mingw32"}
		--General target rules
		configuration "Debug"
			defines {"DEBUG"}
			flags {"Symbols"}
		configuration "Release"
			defines {"NDEBUG"}
			flags {"Optimize"}	
		configuration {}
		--Include these last, since they may be dependent on other libraries 
		--under certain configurations.
		links {"sdl2main", "sdl2"}