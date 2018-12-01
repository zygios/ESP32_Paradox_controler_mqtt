commandArray = {}
DeviceName = 'Paradox_status';

if devicechanged[DeviceName] then
    status = tostring(devicechanged[DeviceName]);
    print("Paradox status changed to:"..status);
    if (status == 'Off') then ParadoxIntStatus = 0
    elseif (status == 'On') then ParadoxIntStatus = 10
    elseif (status == 'On_sleep_mode') then ParadoxIntStatus = 40
    elseif (status == 'On_stay_mode') then ParadoxIntStatus = 50
    else ParadoxIntStatus = 100
    end
    ParadoxJson = '{"DomParadox_Status":"'..tostring(ParadoxIntStatus)..'"}';
    print("Json sending:"..ParadoxJson);
    if ParadoxIntStatus ~= 100 then
        os.execute('mosquitto_pub -h 192.168.1.40 -t paradox/in -m '..ParadoxJson);
    end;
end
return commandArray