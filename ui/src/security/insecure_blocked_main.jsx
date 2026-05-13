import React from 'react'
import ReactDOM from 'react-dom/client'
import InsecureBlocked from './InsecureBlocked'
import '../styles/App.css'

ReactDOM.createRoot(document.getElementById('root')).render(
  <React.StrictMode>
    <InsecureBlocked />
  </React.StrictMode>,
)
