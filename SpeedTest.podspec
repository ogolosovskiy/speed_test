Pod::Spec.new do |s|
  s.name         = "SpeedTest"
  s.version      = "1.0"
  s.summary      = "SpeedTest"
  s.description  = <<-DESC
    PeerScope SpeedTest.
                   DESC
  s.homepage     = "https://steppechange.com/"
  s.author       = {
    "Oleg Golosovskiy" => "oleggl@steppechange.com",
  }

  s.ios.deployment_target  = '9.0'
  s.tvos.deployment_target = '9.0'
  s.source       = { :path => './' }

  s.source_files = "packet.{h,cpp}", "client_lib.{h,cpp}"
  s.public_header_files = "packet.h","client_lib.h"
    
end
