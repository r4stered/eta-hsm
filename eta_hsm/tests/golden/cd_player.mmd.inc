R"DIAGRAM(stateDiagram-v2
    [*] --> Stopped
    Stopped
    Open
    Empty
    Playing
    Paused
    Broken
    Stopped --> Playing : Play / start_playback
    Stopped --> Open : OpenClose / open_drawer
    Open --> Empty : OpenClose / close_drawer
    Empty --> Stopped : CdDetected / store_cd_info
    Empty --> Open : OpenClose / open_drawer
    Playing --> Stopped : Stop / stop_playback
    Playing --> Paused : Pause / pause_playback
    Playing --> Open : OpenClose / stop_and_open
    Paused --> Stopped : Stop / stop_playback
    Paused --> Playing : EndPause / resume_playback
    Paused --> Open : OpenClose / stop_and_open
    Stopped --> Open : Hammer [drawer_jammed] / open_drawer
    Playing : VolumeUp / turn_up
    Playing --> Playing : Next / next_track
    Top --> Playing : Hammer
)DIAGRAM"
