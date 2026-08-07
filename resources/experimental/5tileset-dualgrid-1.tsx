<?xml version="1.0" encoding="UTF-8"?>
<tileset version="1.10" tiledversion="1.12.2" name="5tileset-dualgrid-1" tilewidth="16" tileheight="16" tilecount="6" columns="1">
 <transformations hflip="1" vflip="1" rotate="1" preferuntransformed="0"/>
 <image source="../tilesets/5tileset-dualgrid-1.png" width="16" height="96"/>
 <tile id="0">
  <objectgroup draworder="index" id="2">
   <object id="1" x="8.0625" y="8" width="7.9375" height="8"/>
  </objectgroup>
 </tile>
 <tile id="1">
  <objectgroup draworder="index" id="2">
   <object id="3" x="0.0625" y="8.0625">
    <polygon points="0,0 8,0 7.9375,-8.0625 15.875,-8.125 15.9375,-0.0625 8.0625,0.0625 8.125,7.6875 0,7.875"/>
   </object>
  </objectgroup>
 </tile>
 <tile id="2">
  <objectgroup draworder="index" id="2">
   <object id="1" x="0" y="0" width="8.0625" height="16"/>
  </objectgroup>
 </tile>
 <tile id="3">
  <objectgroup draworder="index" id="2">
   <object id="5" x="0" y="0.125">
    <polygon points="0,0 7.9375,0 8.0625,9.0625 15.9375,8.9375 16,15.8125 -0.0625,15.75"/>
   </object>
  </objectgroup>
 </tile>
 <tile id="4">
  <objectgroup draworder="index" id="2">
   <object id="1" x="0" y="0" width="16" height="16"/>
  </objectgroup>
 </tile>
 <tile id="5">
  <objectgroup draworder="index" id="2">
   <object id="1" x="0" y="0" width="16" height="16"/>
  </objectgroup>
 </tile>
 <wangsets>
  <wangset name="5tiledualset-1" type="mixed" tile="-1">
   <wangcolor name="green" color="#00ed11" tile="-1" probability="1"/>
   <wangtile tileid="0" wangid="0,0,0,1,0,0,0,0"/>
   <wangtile tileid="1" wangid="0,1,0,0,0,1,0,0"/>
   <wangtile tileid="2" wangid="0,0,0,0,0,1,1,1"/>
   <wangtile tileid="3" wangid="0,0,0,1,1,1,1,1"/>
   <wangtile tileid="4" wangid="1,1,1,1,1,1,1,1"/>
   <wangtile tileid="5" wangid="1,1,1,1,1,1,1,1"/>
  </wangset>
  <wangset name="5tiledualsetcorner-1" type="corner" tile="-1">
   <wangcolor name="green" color="#00ed4e" tile="-1" probability="1"/>
   <wangtile tileid="0" wangid="0,0,0,1,0,0,0,0"/>
   <wangtile tileid="1" wangid="0,1,0,0,0,1,0,0"/>
   <wangtile tileid="2" wangid="0,0,0,0,0,1,0,1"/>
   <wangtile tileid="3" wangid="0,0,0,1,0,1,0,1"/>
   <wangtile tileid="4" wangid="0,1,0,1,0,1,0,1"/>
  </wangset>
 </wangsets>
</tileset>
