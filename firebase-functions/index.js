const functions = require('firebase-functions');
const admin = require('firebase-admin');
admin.initializeApp();

exports.generateAIAdvice = functions.database.ref('/sensor_data/{pushId}')
  .onCreate(async (snapshot, context) => {
    const sensorData = snapshot.val();
    
    // Get last 10 readings for analysis
    const recentDataSnapshot = await admin.database()
      .ref('/sensor_data')
      .orderByChild('timestamp')
      .limitToLast(10)
      .once('value');
    
    const recentData = [];
    recentDataSnapshot.forEach(childSnapshot => {
      recentData.push(childSnapshot.val());
    });
    
    // Calculate averages
    const avgTemp = recentData.reduce((sum, data) => sum + data.temperature, 0) / recentData.length;
    const avgHumidity = recentData.reduce((sum, data) => sum + data.humidity, 0) / recentData.length;
    const avgMQ135 = recentData.reduce((sum, data) => sum + data.mq135, 0) / recentData.length;
    
    // Generate advice based on current data and trends
    let advice = generateAdvice(sensorData, { 
      avgTemp, 
      avgHumidity, 
      avgMQ135 
    });
    
    // Store the advice
    return admin.database().ref('/ai_advice').update({
      latest: advice,
      timestamp: admin.database.ServerValue.TIMESTAMP
    });
  });

function generateAdvice(currentData, averages) {
  let advice = "";
  
  // Temperature advice
  if (currentData.temperature > 30) {
    advice += "High temperature detected. Consider cooling the room. ";
  } else if (currentData.temperature < 15) {
    advice += "Low temperature detected. Consider heating the room. ";
  } else {
    advice += "Temperature is in a comfortable range. ";
  }
  
  // Humidity advice
  if (currentData.humidity > 70) {
    advice += "High humidity may promote mold growth. Consider using a dehumidifier. ";
  } else if (currentData.humidity < 30) {
    advice += "Low humidity may cause dry skin and respiratory issues. Consider using a humidifier. ";
  } else {
    advice += "Humidity is at a good level. ";
  }
  
  // Air quality advice
  if (currentData.mq135 > 600) {
    advice += "Air quality is poor. Ventilate the room immediately. ";
    if (averages.avgMQ135 > 500) {
      advice += "Air quality has been consistently poor. Consider an air purifier or identifying pollution sources. ";
    }
  } else if (currentData.mq135 > 400) {
    advice += "Moderate air quality. Increasing ventilation is recommended. ";
  } else if (currentData.mq135 > 200) {
    advice += "Air quality is normal. Regular ventilation is still recommended. ";
  } else {
    advice += "Air quality is good. ";
  }
  
  // Trends
  if (currentData.mq135 > averages.avgMQ135 * 1.2) {
    advice += "Air quality is declining compared to recent readings. ";
  } else if (currentData.mq135 < averages.avgMQ135 * 0.8) {
    advice += "Air quality is improving compared to recent readings. ";
  }
  
  return advice;
} 