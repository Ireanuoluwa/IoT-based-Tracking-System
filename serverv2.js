const express = require('express')
const cors = require('cors')
const bodyParser = require('body-parser')
const methodOverride = require('method-override')
const MongoClient = require('mongodb').MongoClient
const app = express()
const port = 8000

app.use(cors())
app.use(express.json())

const connectionString = 'mongodb://localhost:27017'
let db

// Connect to MongoDB
MongoClient.connect(connectionString, {
  useNewUrlParser: true,
  useUnifiedTopology: true,
})
  .then((client) => {
    db = client.db('trackingData')
    console.log('Connected to Database')
  })
  .catch((error) => {
    console.error('Database connection error:', error)
    process.exit(1) // Exit the process if the database connection fails
  })

app.post('/api/tracking', (req, res) => {
  const data = req.body
  console.log('Received data:', data)

  db.collection('tracking').insertOne(data, (err, result) => {
    if (err) return res.status(500).send('Error inserting data.')
    res.json({
      message: 'Data received and saved successfully',
      data: result.ops[0],
    })
  })
})
// Endpoint to toggle the delivered status for a specific clientId
app.put('/api/tracking/:clientId/delivered', (req, res) => {
  const { clientId } = req.params

  db.collection('tracking')
    .findOne({ clientId })
    .then((data) => {
      if (!data) {
        return res
          .status(404)
          .json({ message: 'No tracking data found for this clientId' })
      }

      const newStatus = !data.delivered
      db.collection('tracking')
        .updateOne({ clientId }, { $set: { delivered: newStatus } })
        .then((result) => {
          if (result.matchedCount === 0) {
            return res
              .status(404)
              .json({ message: 'No tracking data found for this clientId' })
          }
          res.json({
            message: `Delivery status updated to ${
              newStatus ? 'Delivered' : 'Not Delivered'
            }`,
            delivered: newStatus,
          })
        })
        .catch((err) => {
          console.error('Error updating delivery status:', err)
          res.status(500).send('Error updating delivery status.')
        })
    })
    .catch((err) => {
      console.error('Error retrieving tracking data:', err)
      res.status(500).send('Error retrieving tracking data.')
    })
})

app.get('/api/tracking/:clientId/status', (req, res) => {
  const { clientId } = req.params

  db.collection('tracking')
    .findOne({ clientId }, { projection: { delivered: 1 } })
    .then((data) => {
      if (!data) {
        return res
          .status(404)
          .json({ message: 'No tracking data found for this clientId' })
      }
      res.json({ delivered: data.delivered })
    })
    .catch((err) => {
      console.error('Error retrieving delivery status:', err)
      res.status(500).send('Error retrieving delivery status.')
    })
})

// Endpoint to get tracking data for a specific clientId
app.get('/api/tracking/:clientId', (req, res) => {
  const { clientId } = req.params

  db.collection('tracking')
    .findOne({ clientId })
    .then((data) => {
      if (!data)
        return res
          .status(404)
          .json({ message: 'No tracking data found for this clientId' })

      console.log(`Retrieved tracking data for clientId ${clientId}:`, data) // Log retrieved data
      res.json(data)
    })
    .catch((err) => {
      console.error('Error retrieving tracking data:', err)
      res.status(500).send('Error retrieving tracking data.')
    })
})

app.get('/api/tracking/:clientId/status', (req, res) => {
  const { clientId } = req.params

  db.collection('tracking')
    .findOne({ clientId }, { projection: { delivered: 1 } })
    .then((data) => {
      if (!data) {
        return res
          .status(404)
          .json({ message: 'No tracking data found for this clientId' })
      }
      res.json({ delivered: data.delivered })
    })
    .catch((err) => {
      console.error('Error retrieving delivery status:', err)
      res.status(500).send('Error retrieving delivery status.')
    })
})

// Endpoint for MCU to send live location updates
app.post('/api/liveLocation', (req, res) => {
  const { clientId, lat, lng } = req.body

  if (!clientId || !lat || !lng) {
    return res.status(400).json({ message: 'Missing required fields' })
  }

  const liveLocationData = { clientId, lat, lng, timestamp: new Date() }

  console.log('Received live location update MOYIN:', liveLocationData) // Log live location update
  db.collection('liveLocation').insertOne(liveLocationData, (err, result) => {
    if (err) return res.status(500).send('Error inserting live location data.')

    res.json({
      message: 'Live location update saved successfully',
      data: result.ops[0],
    })
  })
})

// Endpoint to get the latest live location for a specific clientId
app.get('/api/liveLocation/:clientId', (req, res) => {
  const { clientId } = req.params

  db.collection('liveLocation')
    .find({ clientId })
    .sort({ timestamp: -1 }) // Sort by the latest timestamp
    .limit(1)
    .toArray()
    .then((data) => {
      if (data.length === 0) {
        return res
          .status(404)
          .json({ message: 'No live location updates found for this clientId' })
      }

      console.log(`Latest live location for clientId ${clientId}:`, data[0]) // Log latest live location
      res.json(data[0])
    })
    .catch((err) => {
      console.error('Error retrieving live location data:', err)
      res.status(500).send('Error retrieving live location data.')
    })
})

app.post('/api/battery', (req, res) => {
  const data = req.body
  console.log('Received battery update:', data)

  db.collection('batteryUpdates').insertOne(data, (err, result) => {
    if (err) return res.status(500).send('Error inserting data.')
    res.json({
      message: 'Battery update received and saved successfully',
      data: result.ops[0],
    })
  })
})

// Endpoint to fetch all tracking data
app.get('/api/getClientDetails', (req, res) => {
  db.collection('tracking')
    .findOne({}, { sort: { _id: -1 } }) // Sort by `_id` in descending order to get the latest entry
    .then((data) => {
      if (!data) {
        return res.status(404).json({ message: 'No tracking data found' })
      }
      res.json(data)
    })
    .catch((err) => {
      console.error('Error retrieving data:', err)
      res.status(500).send('Error retrieving data.')
    })
})

// Endpoint to fetch the latest battery update
app.get('/api/battery', (req, res) => {
  db.collection('batteryUpdates')
    .find()
    .sort({ _id: -1 }) // Get the latest update
    .limit(1)
    .toArray()
    .then((data) => {
      if (data.length === 0) {
        return res.status(404).json({ message: 'No battery data found' })
      }
      res.json(data[0]) // Send the latest battery update
    })
    .catch((err) => {
      console.error('Error retrieving battery data:', err)
      res.status(500).send('Error retrieving battery data.')
    })
})

app.use(methodOverride())
app.use(express.static(__dirname + '/public'))

// Start the server
app.listen(port, () => {
  console.log(`Server listening at http://localhost:${port}`)
})
