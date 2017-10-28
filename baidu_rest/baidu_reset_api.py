import json
import httplib
import base64
f=open('8k.amr','rb')
data=f.read(6340)
access_token="24.44810154581d4b7e8cc3554c90b949f0.2592000.1505980562.282335-10037482"
speech=base64.b64encode(data)
length=6340
params={'format':"amr","rate":8000,"channel":1,"cuid":"eps32_frankie","token":access_token,"speech":speech,"len":length}
a=json.dumps(params)
print a
conn = httplib.HTTPConnection("vop.baidu.com",80)
conn.request("GET","/server_api",a)
response=conn.getresponse()
print response.status,response.reason
print response.read()