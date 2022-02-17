Start-Process -FilePath "python" -ArgumentList @("./code/service.py")

$serviceName = "service$env:ServiceId"
& 'C:\\Program Files\\envoy\\envoy.exe' --config-path .\service-envoy.yaml --service-cluster $serviceName
