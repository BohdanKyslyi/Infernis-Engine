private:
    BOOL m_DemoPlay = FALSE;
    BOOL m_DemoPlayStarted = FALSE;
    BOOL m_DemoPlayStoped = FALSE;
    BOOL m_DemoSave = FALSE;
    BOOL m_DemoSaveStarted = FALSE;
    u32 m_StartGlobalTime = 0;
    
    CObject* m_current_spectator = nullptr; // in real, this is CurrentControlEntity
    message_filter* m_msg_filter = nullptr;
    demoplay_control* m_demoplay_control = nullptr;

public:
    void SetDemoSpectator(CObject* spectator);
    [[nodiscard]] CObject* GetDemoSpectator() const;

    void PrepareToSaveDemo();
    void SaveDemoInfo();
    
    [[nodiscard]] inline demo_info* GetDemoInfo() const { return m_demo_info; }
    bool PrepareToPlayDemo(shared_str const& file_name);

    void StartPlayDemo();
    void RestartPlayDemo();
    void StopPlayDemo();

    [[nodiscard]] float GetDemoPlayPos() const;
    [[nodiscard]] float GetDemoPlaySpeed() const;                 
    void SetDemoPlaySpeed(float const time_factor); 
    
    [[nodiscard]] message_filter* GetMessageFilter();
    [[nodiscard]] demoplay_control* GetDemoPlayControl();

    [[nodiscard]] BOOL IsDemoPlay() const { return (!m_DemoSave && m_DemoPlay); }
    [[nodiscard]] BOOL IsDemoSave() const { return (m_DemoSave && !m_DemoPlay); }
    [[nodiscard]] inline BOOL IsDemoPlayStarted() const { return (IsDemoPlay() && m_DemoPlayStarted); }
    [[nodiscard]] inline BOOL IsDemoPlayFinished() const { return m_DemoPlayStoped; }
    [[nodiscard]] inline BOOL IsDemoSaveStarted() const { return (IsDemoSave() && m_DemoSaveStarted); }

#pragma pack(push, 1)
    struct DemoHeader {
        u32 m_time_global;
        u32 m_time_server;
        s32 m_time_delta;
        s32 m_time_delta_user;
    };
    
    struct DemoPacket {
        u32 m_time_global_delta;
        u32 m_timeReceive;
        u32 m_packet_size;
        // here will be body of NET_Packet ...
    };
#pragma pack(pop)

    void SavePacket(NET_Packet& packet);

private:
    void StartSaveDemo(shared_str const& server_options);
    void StopSaveDemo();
    void SpawnDemoSpectator();

    void SaveDemoHeader(shared_str const& server_options);
    [[nodiscard]] inline bool IsDemoInfoSaved() const { return m_demo_info != nullptr; }

    bool LoadDemoHeader();
    bool LoadPacket(NET_Packet& dest_packet, u32 global_time_delta);
    void SimulateServerUpdate();

    void CatchStartingSpawns();
    void __stdcall MSpawnsCatchCallback(u32 message, u32 subtype, NET_Packet& packet);

    DemoHeader m_demo_header{};
    shared_str m_demo_server_options;
    
    // if instance of this class exist, then the demo info have saved or loaded...
    demo_info* m_demo_info = nullptr; 
    u32 m_demo_info_file_pos = 0;
    IWriter* m_writer = nullptr;
    CStreamReader* m_reader = nullptr;

    u32 m_prev_packet_pos = 0;
    u32 m_prev_packet_dtime = 0;

    u32 m_starting_spawns_pos = 0;
    u32 m_starting_spawns_dtime = 0;