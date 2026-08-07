<?xml version="1.0" encoding="UTF-8"?>
<tileset version="1.10" tiledversion="1.12.2" name="5tileset-dualgrid-1" tilewidth="16" tileheight="16" tilecount="12" columns="2">
 <transformations hflip="1" vflip="1" rotate="1" preferuntransformed="0"/>
 <image source="../tilesets/5tileset-dualgrid-1.png" width="32" height="96"/>
 <tile id="0">
  <objectgroup draworder="index" id="2">
   <object id="1" x="8.0625" y="8" width="7.9375" height="8"/>
  </objectgroup>
 </tile>
 <tile id="1">
  <objectgroup draworder="index" id="2">
   <object id="1" x="7" y="6" width="9" height="10"/>
  </objectgroup>
 </tile>
 <tile id="2">
  <objectgroup draworder="index" id="2">
   <object id="3" x="0.0625" y="8.0625">
    <polygon points="0,0 8,0 7.9375,-8.0625 15.875,-8.125 15.9375,-0.0625 8.0625,0.0625 8.125,7.6875 0,7.875"/>
   </object>
  </objectgroup>
 </tile>
 <tile id="3">
  <objectgroup draworder="index" id="2">
   <object id="1" x="0.1875" y="8">
    <polygon points="0,0 6.6875,0 6.75,-7.9375 15.75,-8 15.875,1.8125 7.875,1.9375 7.875,8.0625 -0.0625,8"/>
   </object>
  </objectgroup>
 </tile>
 <tile id="4">
  <objectgroup draworder="index" id="2">
   <object id="1" x="0" y="0" width="8.0625" height="16"/>
  </objectgroup>
 </tile>
 <tile id="5">
  <objectgroup draworder="index" id="2">
   <object id="1" x="0" y="0" width="9" height="16"/>
  </objectgroup>
 </tile>
 <tile id="6">
  <objectgroup draworder="index" id="2">
   <object id="5" x="0" y="0.125">
    <polygon points="0,0 7.9375,0 8.0625,9.0625 15.9375,8.9375 16,15.8125 -0.0625,15.75"/>
   </object>
  </objectgroup>
 </tile>
 <tile id="7">
  <objectgroup draworder="index" id="2">
   <object id="1" x="0.0625" y="-0.125">
    <polygon points="0,0 8,0.125 8.25,10.375 15.875,10.1875 15.875,16.25 -0.125,16"/>
   </object>
  </objectgroup>
 </tile>
 <tile id="8">
  <objectgroup draworder="index" id="2">
   <object id="1" x="0" y="0" width="16" height="16"/>
  </objectgroup>
 </tile>
 <tile id="9">
  <objectgroup draworder="index" id="2">
   <object id="1" x="0" y="0" width="16" height="16"/>
  </objectgroup>
 </tile>
 <tile id="10">
  <objectgroup draworder="index" id="2">
   <object id="1" x="0" y="0" width="16" height="16"/>
  </objectgroup>
 </tile>
 <tile id="11">
  <objectgroup draworder="index" id="2">
   <object id="1" x="0" y="0" width="16" height="16"/>
  </objectgroup>
 </tile>
 <wangsets>
  <wangset name="5tiledualset-1" type="mixed" tile="-1">
   <wangcolor name="green" color="#00ed11" tile="-1" probability="1"/>
   <wangcolor name="tree" color="#964000" tile="-1" probability="1"/>
   <wangtile tileid="0" wangid="0,0,0,1,0,0,0,0"/>
   <wangtile tileid="1" wangid="0,0,0,2,0,0,0,0"/>
   <wangtile tileid="2" wangid="0,1,0,0,0,1,0,0"/>
   <wangtile tileid="3" wangid="0,2,0,0,0,2,0,0"/>
   <wangtile tileid="4" wangid="0,0,0,0,0,1,1,1"/>
   <wangtile tileid="5" wangid="0,0,0,0,0,2,2,2"/>
   <wangtile tileid="6" wangid="0,0,0,1,1,1,1,1"/>
   <wangtile tileid="7" wangid="0,0,0,2,2,2,2,2"/>
   <wangtile tileid="8" wangid="1,1,1,1,1,1,1,1"/>
   <wangtile tileid="9" wangid="2,2,2,2,2,2,2,2"/>
   <wangtile tileid="10" wangid="1,1,1,1,1,1,1,1"/>
   <wangtile tileid="11" wangid="2,2,2,2,2,2,2,2"/>
  </wangset>
  <wangset name="5tiledualsetcorner-1" type="corner" tile="-1">
   <wangcolor name="green" color="#00ed4e" tile="-1" probability="1"/>
   <wangtile tileid="0" wangid="0,0,0,1,0,0,0,0"/>
   <wangtile tileid="2" wangid="0,1,0,0,0,1,0,0"/>
   <wangtile tileid="4" wangid="0,0,0,0,0,1,0,1"/>
   <wangtile tileid="6" wangid="0,0,0,1,0,1,0,1"/>
   <wangtile tileid="8" wangid="0,1,0,1,0,1,0,1"/>
  </wangset>
 </wangsets>
</tileset>
